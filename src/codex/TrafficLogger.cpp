#include "codex/TrafficLogger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>

namespace qodex::codex {

namespace {

std::optional<qint64> elapsedFrom(const QElapsedTimer &timer) {
    if (!timer.isValid()) {
        return std::nullopt;
    }
    return timer.elapsed();
}

}  // namespace

TrafficLogger::TrafficLogger(
    qodex::storage::DatabaseManager *databaseManager,
    AppServerTransport *transport,
    QObject *parent
)
    : QObject(parent),
      m_databaseManager(databaseManager),
      m_transport(transport),
      m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    Q_ASSERT(m_transport != nullptr);

    connect(m_transport, &AppServerTransport::notificationSent, this, [this](const JsonRpcNotificationMessage &message) {
        logNotification(QStringLiteral("outbound"), message);
    });
    connect(m_transport, &AppServerTransport::requestSent, this, [this](const JsonRpcRequestMessage &message) {
        PendingRequestContext context{
            .method = message.method,
            .threadId = extractThreadId(message.raw),
        };
        context.timer.start();
        m_outboundRequests.insert(message.id.toKey(), context);
        logRequest(QStringLiteral("outbound"), message);
    });
    connect(m_transport, &AppServerTransport::responseSent, this, [this](const JsonRpcResponseMessage &message) {
        logResponse(QStringLiteral("outbound"), message, true);
    });
    connect(m_transport, &AppServerTransport::errorResponseSent, this, [this](const JsonRpcErrorResponseMessage &message) {
        logErrorResponse(QStringLiteral("outbound"), message);
    });

    connect(m_transport, &AppServerTransport::notificationReceived, this, [this](const JsonRpcNotificationMessage &message) {
        logNotification(QStringLiteral("inbound"), message);
    });
    connect(m_transport, &AppServerTransport::requestReceived, this, [this](const JsonRpcRequestMessage &message) {
        PendingRequestContext context{
            .method = message.method,
            .threadId = extractThreadId(message.raw),
        };
        context.timer.start();
        m_inboundRequests.insert(message.id.toKey(), context);
        logRequest(QStringLiteral("inbound"), message);
    });
    connect(m_transport, &AppServerTransport::responseReceived, this, [this](const JsonRpcResponseMessage &message) {
        logResponse(QStringLiteral("inbound"), message, true);
    });
    connect(m_transport, &AppServerTransport::errorResponseReceived, this, [this](const JsonRpcErrorResponseMessage &message) {
        logErrorResponse(QStringLiteral("inbound"), message);
    });
    connect(m_transport, &AppServerTransport::transportError, this, &TrafficLogger::logTransportError);
}

QString TrafficLogger::sessionId() const {
    return m_sessionId;
}

void TrafficLogger::logNotification(const QString &direction, const JsonRpcNotificationMessage &message) {
    const QString threadId = extractThreadId(message.raw);
    writeRecord({
        .sessionId = m_sessionId,
        .direction = direction,
        .messageKind = QStringLiteral("notification"),
        .method = message.method,
        .jsonrpcId = QString(),
        .correlationId = QString(),
        .threadId = threadId,
        .success = std::nullopt,
        .latencyMs = std::nullopt,
        .payloadJson = serializeObject(message.raw),
        .summaryText = buildSummary(
            direction,
            QStringLiteral("notification"),
            message.method,
            QString(),
            threadId,
            std::nullopt,
            std::nullopt
        ),
    });
}

void TrafficLogger::logRequest(
    const QString &direction,
    const JsonRpcRequestMessage &message,
    const std::optional<bool> success,
    const std::optional<qint64> latencyMs
) {
    const QString jsonrpcId = message.id.toDisplayString();
    const QString threadId = extractThreadId(message.raw);
    writeRecord({
        .sessionId = m_sessionId,
        .direction = direction,
        .messageKind = QStringLiteral("request"),
        .method = message.method,
        .jsonrpcId = jsonrpcId,
        .correlationId = jsonrpcId,
        .threadId = threadId,
        .success = success,
        .latencyMs = latencyMs,
        .payloadJson = serializeObject(message.raw),
        .summaryText = buildSummary(
            direction,
            QStringLiteral("request"),
            message.method,
            jsonrpcId,
            threadId,
            success,
            latencyMs
        ),
    });
}

void TrafficLogger::logResponse(
    const QString &direction,
    const JsonRpcResponseMessage &message,
    const bool success
) {
    const QString jsonrpcId = message.id.toDisplayString();
    PendingRequestContext context;
    bool hasContext = false;

    if (direction == QStringLiteral("inbound")) {
        const auto it = m_outboundRequests.find(message.id.toKey());
        if (it != m_outboundRequests.end()) {
            context = it.value();
            m_outboundRequests.erase(it);
            hasContext = true;
        }
    } else {
        const auto it = m_inboundRequests.find(message.id.toKey());
        if (it != m_inboundRequests.end()) {
            context = it.value();
            m_inboundRequests.erase(it);
            hasContext = true;
        }
    }

    const QString threadId = hasContext && !context.threadId.isEmpty()
        ? context.threadId
        : extractThreadId(message.raw);
    writeRecord({
        .sessionId = m_sessionId,
        .direction = direction,
        .messageKind = QStringLiteral("response"),
        .method = hasContext ? context.method : QString(),
        .jsonrpcId = jsonrpcId,
        .correlationId = jsonrpcId,
        .threadId = threadId,
        .success = success,
        .latencyMs = hasContext ? elapsedFrom(context.timer) : std::nullopt,
        .payloadJson = serializeObject(message.raw),
        .summaryText = buildSummary(
            direction,
            QStringLiteral("response"),
            hasContext ? context.method : QString(),
            jsonrpcId,
            threadId,
            success,
            hasContext ? elapsedFrom(context.timer) : std::nullopt
        ),
    });
}

void TrafficLogger::logErrorResponse(
    const QString &direction,
    const JsonRpcErrorResponseMessage &message
) {
    const QString jsonrpcId = message.id.toDisplayString();
    PendingRequestContext context;
    bool hasContext = false;

    if (direction == QStringLiteral("inbound")) {
        const auto it = m_outboundRequests.find(message.id.toKey());
        if (it != m_outboundRequests.end()) {
            context = it.value();
            m_outboundRequests.erase(it);
            hasContext = true;
        }
    } else {
        const auto it = m_inboundRequests.find(message.id.toKey());
        if (it != m_inboundRequests.end()) {
            context = it.value();
            m_inboundRequests.erase(it);
            hasContext = true;
        }
    }

    const QString threadId = hasContext && !context.threadId.isEmpty()
        ? context.threadId
        : extractThreadId(message.raw);
    const std::optional<qint64> latencyMs = hasContext ? elapsedFrom(context.timer) : std::nullopt;
    writeRecord({
        .sessionId = m_sessionId,
        .direction = direction,
        .messageKind = QStringLiteral("error"),
        .method = hasContext ? context.method : QString(),
        .jsonrpcId = jsonrpcId,
        .correlationId = jsonrpcId,
        .threadId = threadId,
        .success = false,
        .latencyMs = latencyMs,
        .payloadJson = serializeObject(message.raw),
        .summaryText = buildSummary(
            direction,
            QStringLiteral("error"),
            hasContext ? context.method : QString(),
            jsonrpcId,
            threadId,
            false,
            latencyMs,
            message.error.code
        ),
    });
}

void TrafficLogger::writeRecord(const qodex::storage::ApiLogRecord &record) {
    if (m_databaseManager == nullptr || !m_databaseManager->isOpen()) {
        return;
    }

    QString errorMessage;
    if (!m_databaseManager->appendApiLog(record, &errorMessage)) {
        qWarning("Failed to append API log: %s", qPrintable(errorMessage));
        return;
    }

    emit apiLogRecorded();
}

void TrafficLogger::logTransportError(const QString &message) {
    writeRecord({
        .sessionId = m_sessionId,
        .direction = QStringLiteral("transport"),
        .messageKind = QStringLiteral("transport_error"),
        .method = QString(),
        .jsonrpcId = QString(),
        .correlationId = QString(),
        .threadId = QString(),
        .success = false,
        .latencyMs = std::nullopt,
        .payloadJson = serializeObject(QJsonObject{
            {QStringLiteral("message"), message},
        }),
        .summaryText = message,
    });
}

QString TrafficLogger::serializeObject(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString TrafficLogger::buildSummary(
    const QString &direction,
    const QString &messageKind,
    const QString &method,
    const QString &jsonrpcId,
    const QString &threadId,
    const std::optional<bool> success,
    const std::optional<qint64> latencyMs,
    const std::optional<qint64> errorCode
) {
    QString summary = direction + QLatin1Char(' ') + messageKind;
    if (!method.isEmpty()) {
        summary += QLatin1Char(' ') + method;
    }
    if (!jsonrpcId.isEmpty()) {
        summary += QStringLiteral(" id=%1").arg(jsonrpcId);
    }
    if (!threadId.isEmpty()) {
        summary += QStringLiteral(" thread=%1").arg(threadId);
    }
    if (success.has_value()) {
        summary += *success ? QStringLiteral(" ok") : QStringLiteral(" failed");
    }
    if (latencyMs.has_value()) {
        summary += QStringLiteral(" %1ms").arg(*latencyMs);
    }
    if (errorCode.has_value()) {
        summary += QStringLiteral(" code=%1").arg(*errorCode);
    }
    return summary;
}

QString TrafficLogger::extractThreadId(const QJsonValue &value) {
    if (value.isObject()) {
        return extractThreadId(value.toObject());
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            const QString threadId = extractThreadId(item);
            if (!threadId.isEmpty()) {
                return threadId;
            }
        }
    }
    return {};
}

QString TrafficLogger::extractThreadId(const QJsonObject &object) {
    for (const QString &key : {QStringLiteral("threadId"), QStringLiteral("thread_id")}) {
        const auto it = object.find(key);
        if (it != object.end() && it->isString()) {
            return it->toString();
        }
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString threadId = extractThreadId(it.value());
        if (!threadId.isEmpty()) {
            return threadId;
        }
    }
    return {};
}

}  // namespace qodex::codex
