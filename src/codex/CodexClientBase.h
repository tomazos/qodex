#pragma once

#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

#include <functional>

#include "AppServerTransport.h"
#include "JsonRpcMessage.h"

namespace qodex::codex {

class CodexClientBase : public QObject {
    Q_OBJECT

public:
    using SuccessCallback = std::function<void(const JsonRpcId &, const QJsonValue &)>;
    using ErrorCallback = std::function<void(const JsonRpcId &, const JsonRpcErrorObject &)>;

    explicit CodexClientBase(AppServerTransport *transport, QObject *parent = nullptr);
    ~CodexClientBase() override = default;

    [[nodiscard]] AppServerTransport *transport() const;
    bool sendServerResponse(const JsonRpcId &id, const QJsonValue &result);
    bool sendServerError(
        const JsonRpcId &id,
        qint64 code,
        const QString &message,
        const QJsonValue &data = QJsonValue(QJsonValue::Undefined)
    );

signals:
    void unhandledServerNotification(const qodex::codex::JsonRpcNotificationMessage &message);
    void unhandledServerRequest(const qodex::codex::JsonRpcRequestMessage &message);
    void transportErrorOccurred(const QString &message);
    void transportProcessExited(int exitCode, QProcess::ExitStatus exitStatus);

protected:
    JsonRpcId sendClientRequest(
        const QString &method,
        const QJsonValue &params = QJsonValue(QJsonValue::Undefined),
        SuccessCallback onSuccess = {},
        ErrorCallback onError = {}
    );
    bool sendClientNotification(
        const QString &method,
        const QJsonValue &params = QJsonValue(QJsonValue::Undefined)
    );

    virtual bool handleServerNotification(const JsonRpcNotificationMessage &message) = 0;
    virtual bool handleServerRequest(const JsonRpcRequestMessage &message) = 0;

private slots:
    void onTransportNotificationReceived(const qodex::codex::JsonRpcNotificationMessage &message);
    void onTransportRequestReceived(const qodex::codex::JsonRpcRequestMessage &message);

private:
    QPointer<AppServerTransport> m_transport;
};

}  // namespace qodex::codex
