#include "CodexClientBase.h"

namespace qodex::codex {

CodexClientBase::CodexClientBase(AppServerTransport *transport, QObject *parent)
    : QObject(parent),
      m_transport(transport) {
    Q_ASSERT(m_transport);

    if (!m_transport) {
        return;
    }

    connect(
        m_transport,
        &AppServerTransport::notificationReceived,
        this,
        &CodexClientBase::onTransportNotificationReceived
    );
    connect(
        m_transport,
        &AppServerTransport::requestReceived,
        this,
        &CodexClientBase::onTransportRequestReceived
    );
    connect(
        m_transport,
        &AppServerTransport::transportError,
        this,
        &CodexClientBase::transportErrorOccurred
    );
    connect(
        m_transport,
        &AppServerTransport::processExited,
        this,
        &CodexClientBase::transportProcessExited
    );
}

AppServerTransport *CodexClientBase::transport() const {
    return m_transport.data();
}

JsonRpcId CodexClientBase::sendClientRequest(
    const QString &method,
    const QJsonValue &params,
    SuccessCallback onSuccess,
    ErrorCallback onError
) {
    if (!m_transport) {
        emit transportErrorOccurred(QStringLiteral("CodexClientBase has no AppServerTransport."));
        return {};
    }

    return m_transport->sendRequest(
        method,
        params,
        this,
        [onSuccess = std::move(onSuccess)](const JsonRpcResponseMessage &response) {
            if (onSuccess) {
                onSuccess(response.id, response.result);
            }
        },
        [onError = std::move(onError)](const JsonRpcErrorResponseMessage &response) {
            if (onError) {
                onError(response.id, response.error);
            }
        }
    );
}

bool CodexClientBase::sendClientNotification(const QString &method, const QJsonValue &params) {
    if (!m_transport) {
        emit transportErrorOccurred(QStringLiteral("CodexClientBase has no AppServerTransport."));
        return false;
    }
    return m_transport->sendNotification(method, params);
}

bool CodexClientBase::sendServerResponse(const JsonRpcId &id, const QJsonValue &result) {
    if (!m_transport) {
        emit transportErrorOccurred(QStringLiteral("CodexClientBase has no AppServerTransport."));
        return false;
    }
    return m_transport->sendResponse(id, result);
}

bool CodexClientBase::sendServerError(
    const JsonRpcId &id,
    const qint64 code,
    const QString &message,
    const QJsonValue &data
) {
    if (!m_transport) {
        emit transportErrorOccurred(QStringLiteral("CodexClientBase has no AppServerTransport."));
        return false;
    }
    return m_transport->sendError(id, code, message, data);
}

void CodexClientBase::onTransportNotificationReceived(const JsonRpcNotificationMessage &message) {
    if (!handleServerNotification(message)) {
        emit unhandledServerNotification(message);
    }
}

void CodexClientBase::onTransportRequestReceived(const JsonRpcRequestMessage &message) {
    if (!handleServerRequest(message)) {
        emit unhandledServerRequest(message);
    }
}

}  // namespace qodex::codex
