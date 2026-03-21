#include "AppServerTransport.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace qodex::codex {

AppServerTransport::AppServerTransport(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<JsonRpcId>();
    qRegisterMetaType<JsonRpcErrorObject>();
    qRegisterMetaType<JsonRpcNotificationMessage>();
    qRegisterMetaType<JsonRpcRequestMessage>();
    qRegisterMetaType<JsonRpcResponseMessage>();
    qRegisterMetaType<JsonRpcErrorResponseMessage>();

    connect(&m_process, &QProcess::started, this, &AppServerTransport::started);
    connect(
        &m_process,
        &QProcess::readyReadStandardOutput,
        this,
        &AppServerTransport::handleReadyReadStandardOutput
    );
    connect(
        &m_process,
        &QProcess::readyReadStandardError,
        this,
        &AppServerTransport::handleReadyReadStandardError
    );
    connect(
        &m_process,
        &QProcess::errorOccurred,
        this,
        &AppServerTransport::handleProcessError
    );
    connect(
        &m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        &AppServerTransport::handleProcessFinished
    );
}

AppServerTransport::~AppServerTransport() {
    stop();
}

void AppServerTransport::start(const QString &program, const QStringList &arguments) {
    if (isRunning()) {
        stop();
    }

    m_program = program;
    m_arguments = arguments;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_pendingRequests.clear();

    m_process.setProgram(m_program);
    m_process.setArguments(m_arguments);
    m_process.start();
}

void AppServerTransport::stop() {
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }

    m_process.closeWriteChannel();
    m_process.terminate();
    if (!m_process.waitForFinished(3000)) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}

bool AppServerTransport::isRunning() const {
    return m_process.state() != QProcess::NotRunning;
}

QString AppServerTransport::program() const {
    return m_program;
}

QStringList AppServerTransport::arguments() const {
    return m_arguments;
}

JsonRpcId AppServerTransport::sendRequest(
    const QString &method,
    const QJsonValue &params,
    QObject *context,
    ResponseHandler onSuccess,
    ErrorHandler onError
) {
    if (!isRunning()) {
        emit transportError(QStringLiteral("Cannot send request while app-server is not running."));
        return {};
    }

    const JsonRpcId id = allocateRequestId();
    QJsonObject object{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id.value},
        {QStringLiteral("method"), method},
    };
    if (!params.isUndefined()) {
        object.insert(QStringLiteral("params"), params);
    }

    m_pendingRequests.insert(
        id.toKey(),
        PendingRequest{
            .id = id,
            .hasContext = context != nullptr,
            .context = context,
            .onSuccess = std::move(onSuccess),
            .onError = std::move(onError),
        }
    );
    writeMessage(object);
    return id;
}

bool AppServerTransport::sendNotification(const QString &method, const QJsonValue &params) {
    if (!isRunning()) {
        emit transportError(QStringLiteral("Cannot send notification while app-server is not running."));
        return false;
    }

    QJsonObject object{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
    };
    if (!params.isUndefined()) {
        object.insert(QStringLiteral("params"), params);
    }
    writeMessage(object);
    return true;
}

bool AppServerTransport::sendResponse(const JsonRpcId &id, const QJsonValue &result) {
    if (!isRunning()) {
        emit transportError(QStringLiteral("Cannot send response while app-server is not running."));
        return false;
    }

    QJsonObject object{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id.value},
        {QStringLiteral("result"), result},
    };
    writeMessage(object);
    return true;
}

bool AppServerTransport::sendError(
    const JsonRpcId &id,
    const qint64 code,
    const QString &message,
    const QJsonValue &data
) {
    if (!isRunning()) {
        emit transportError(QStringLiteral("Cannot send error while app-server is not running."));
        return false;
    }

    QJsonObject error{
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
    };
    if (!data.isUndefined()) {
        error.insert(QStringLiteral("data"), data);
    }

    QJsonObject object{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id.value},
        {QStringLiteral("error"), error},
    };
    writeMessage(object);
    return true;
}

void AppServerTransport::handleReadyReadStandardOutput() {
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    drainCompleteLines(m_stdoutBuffer, [this](const QByteArray &line) {
        processIncomingLine(line);
    });
}

void AppServerTransport::handleReadyReadStandardError() {
    m_stderrBuffer.append(m_process.readAllStandardError());
    drainCompleteLines(m_stderrBuffer, [this](const QByteArray &line) {
        processStandardErrorChunk(line);
    });
}

void AppServerTransport::handleProcessError(const QProcess::ProcessError error) {
    emit transportError(describeProcessError(error));
}

void AppServerTransport::handleProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    flushTrailingBuffer(m_stdoutBuffer, [this](const QByteArray &line) {
        processIncomingLine(line);
    });
    flushTrailingBuffer(m_stderrBuffer, [this](const QByteArray &line) {
        processStandardErrorChunk(line);
    });

    failPendingRequests(QStringLiteral("app-server exited before responding to all requests"));
    emit processExited(exitCode, exitStatus);
}

JsonRpcId AppServerTransport::allocateRequestId() {
    const QString requestId = QStringLiteral("client-%1").arg(m_nextRequestSequence++);
    return JsonRpcId{QJsonValue(requestId)};
}

void AppServerTransport::writeMessage(const QJsonObject &message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (m_process.write(payload) == -1) {
        emit transportError(QStringLiteral("Failed to write JSON-RPC message to app-server stdin."));
    }
}

void AppServerTransport::processIncomingLine(const QByteArray &line) {
    const QByteArray trimmedLine = line.trimmed();
    if (trimmedLine.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmedLine, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit transportError(
            QStringLiteral("Failed to parse JSON-RPC line: %1")
                .arg(parseError.errorString())
        );
        return;
    }
    if (!document.isObject()) {
        emit transportError(QStringLiteral("Received non-object JSON-RPC message from app-server."));
        return;
    }
    processIncomingObject(document.object());
}

void AppServerTransport::processIncomingObject(const QJsonObject &object) {
    const JsonRpcId id = parseId(object);
    const bool hasMethod = object.contains(QStringLiteral("method"));
    const bool hasResult = object.contains(QStringLiteral("result"));
    const bool hasError = object.contains(QStringLiteral("error"));

    if (hasMethod) {
        const QString method = object.value(QStringLiteral("method")).toString();
        const QJsonValue params = object.value(QStringLiteral("params"));
        if (id.isValid()) {
            emit requestReceived(JsonRpcRequestMessage{
                .id = id,
                .method = method,
                .params = params,
                .raw = object,
            });
            return;
        }
        emit notificationReceived(JsonRpcNotificationMessage{
            .method = method,
            .params = params,
            .raw = object,
        });
        return;
    }

    if (hasResult && id.isValid()) {
        const JsonRpcResponseMessage response{
            .id = id,
            .result = object.value(QStringLiteral("result")),
            .raw = object,
        };
        const QString key = id.toKey();
        if (m_pendingRequests.contains(key)) {
            const PendingRequest pending = m_pendingRequests.take(key);
            if (!pending.hasContext || !pending.context.isNull()) {
                if (pending.onSuccess) {
                    pending.onSuccess(response);
                }
            }
        }
        emit responseReceived(response);
        return;
    }

    if (hasError && id.isValid()) {
        const QJsonObject errorObject = object.value(QStringLiteral("error")).toObject();
        const JsonRpcErrorResponseMessage errorResponse{
            .id = id,
            .error = JsonRpcErrorObject{
                .code = errorObject.value(QStringLiteral("code")).toInteger(),
                .message = errorObject.value(QStringLiteral("message")).toString(),
                .data = errorObject.value(QStringLiteral("data")),
            },
            .raw = object,
        };
        const QString key = id.toKey();
        if (m_pendingRequests.contains(key)) {
            const PendingRequest pending = m_pendingRequests.take(key);
            if (!pending.hasContext || !pending.context.isNull()) {
                if (pending.onError) {
                    pending.onError(errorResponse);
                }
            }
        }
        emit errorResponseReceived(errorResponse);
        return;
    }

    emit transportError(QStringLiteral("Received JSON-RPC message with unrecognized shape."));
}

void AppServerTransport::processStandardErrorChunk(const QByteArray &chunk) {
    const QString text = QString::fromUtf8(chunk).trimmed();
    if (!text.isEmpty()) {
        emit transportError(QStringLiteral("app-server stderr: %1").arg(text));
    }
}

void AppServerTransport::drainCompleteLines(
    QByteArray &buffer,
    const std::function<void(const QByteArray &)> &lineHandler
) {
    while (true) {
        const qsizetype newlineIndex = buffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        const QByteArray line = buffer.left(newlineIndex);
        buffer.remove(0, newlineIndex + 1);
        lineHandler(line);
    }
}

void AppServerTransport::flushTrailingBuffer(
    QByteArray &buffer,
    const std::function<void(const QByteArray &)> &lineHandler
) {
    const QByteArray trailing = buffer.trimmed();
    buffer.clear();
    if (!trailing.isEmpty()) {
        lineHandler(trailing);
    }
}

void AppServerTransport::failPendingRequests(const QString &message) {
    if (m_pendingRequests.isEmpty()) {
        return;
    }

    const QList<QString> keys = m_pendingRequests.keys();
    for (const QString &key : keys) {
        const PendingRequest pending = m_pendingRequests.take(key);
        if (!pending.onError) {
            continue;
        }
        if (pending.hasContext && pending.context.isNull()) {
            continue;
        }
        pending.onError(JsonRpcErrorResponseMessage{
            .id = pending.id,
            .error = JsonRpcErrorObject{
                .code = -32000,
                .message = message,
                .data = QJsonValue(QJsonValue::Undefined),
            },
            .raw = QJsonObject{},
        });
    }
}

JsonRpcId AppServerTransport::parseId(const QJsonObject &object) {
    if (!object.contains(QStringLiteral("id"))) {
        return {};
    }
    return JsonRpcId{object.value(QStringLiteral("id"))};
}

QString AppServerTransport::describeProcessError(const QProcess::ProcessError error) {
    switch (error) {
    case QProcess::FailedToStart:
        return QStringLiteral("Failed to start codex app-server process.");
    case QProcess::Crashed:
        return QStringLiteral("codex app-server process crashed.");
    case QProcess::Timedout:
        return QStringLiteral("codex app-server process operation timed out.");
    case QProcess::WriteError:
        return QStringLiteral("Write error talking to codex app-server.");
    case QProcess::ReadError:
        return QStringLiteral("Read error talking to codex app-server.");
    case QProcess::UnknownError:
        return QStringLiteral("Unknown error talking to codex app-server.");
    }
    return QStringLiteral("Unhandled QProcess error talking to codex app-server.");
}

}  // namespace qodex::codex
