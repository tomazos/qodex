#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

#include "JsonRpcMessage.h"

namespace qodex::codex {

class AppServerTransport final : public QObject {
    Q_OBJECT

public:
    using ResponseHandler = std::function<void(const JsonRpcResponseMessage &)>;
    using ErrorHandler = std::function<void(const JsonRpcErrorResponseMessage &)>;

    explicit AppServerTransport(QObject *parent = nullptr);
    ~AppServerTransport() override;

    void start(
        const QString &program = QStringLiteral("codex"),
        const QStringList &arguments = QStringList{QStringLiteral("app-server")}
    );
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString program() const;
    [[nodiscard]] QStringList arguments() const;

    JsonRpcId sendRequest(
        const QString &method,
        const QJsonValue &params = QJsonValue(QJsonValue::Undefined),
        QObject *context = nullptr,
        ResponseHandler onSuccess = {},
        ErrorHandler onError = {}
    );
    bool sendNotification(
        const QString &method,
        const QJsonValue &params = QJsonValue(QJsonValue::Undefined)
    );
    bool sendResponse(const JsonRpcId &id, const QJsonValue &result);
    bool sendError(
        const JsonRpcId &id,
        qint64 code,
        const QString &message,
        const QJsonValue &data = QJsonValue(QJsonValue::Undefined)
    );

signals:
    void started();
    void notificationSent(const qodex::codex::JsonRpcNotificationMessage &message);
    void requestSent(const qodex::codex::JsonRpcRequestMessage &message);
    void responseSent(const qodex::codex::JsonRpcResponseMessage &message);
    void errorResponseSent(const qodex::codex::JsonRpcErrorResponseMessage &message);
    void notificationReceived(const qodex::codex::JsonRpcNotificationMessage &message);
    void requestReceived(const qodex::codex::JsonRpcRequestMessage &message);
    void responseReceived(const qodex::codex::JsonRpcResponseMessage &message);
    void errorResponseReceived(const qodex::codex::JsonRpcErrorResponseMessage &message);
    void transportError(const QString &message);
    void processExited(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct PendingRequest {
        JsonRpcId id;
        bool hasContext = false;
        QPointer<QObject> context;
        ResponseHandler onSuccess;
        ErrorHandler onError;
    };

    JsonRpcId allocateRequestId();
    void writeMessage(const QJsonObject &message);
    void processIncomingLine(const QByteArray &line);
    void processIncomingObject(const QJsonObject &object);
    void processStandardErrorChunk(const QByteArray &chunk);
    void drainCompleteLines(QByteArray &buffer, const std::function<void(const QByteArray &)> &lineHandler);
    void flushTrailingBuffer(QByteArray &buffer, const std::function<void(const QByteArray &)> &lineHandler);
    void failPendingRequests(const QString &message);

    static JsonRpcId parseId(const QJsonObject &object);
    static QString describeProcessError(QProcess::ProcessError error);

    QProcess m_process;
    QString m_program = QStringLiteral("codex");
    QStringList m_arguments{QStringLiteral("app-server")};
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    quint64 m_nextRequestSequence = 1;
    QHash<QString, PendingRequest> m_pendingRequests;
};

}  // namespace qodex::codex
