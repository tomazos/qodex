#include "threadui/ThreadUiIpcServer.h"

#include <QByteArray>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include "common.pb.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.pb.h"

namespace qodex::threadui {

namespace {

using qodex::threadui::ipc::FrameDecodeResult;
using qodex::threadui::ipc::common::RESULT_STATUS_ERROR;
using qodex::threadui::ipc::common::RESULT_STATUS_OK;

qodex::threadui::ipc::common::RpcEnvelope makeLoginResponseEnvelope(
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const QString &message
) {
    qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
    response.set_status(status);
    response.set_message(message.toStdString());

    qodex::threadui::ipc::common::RpcEnvelope envelope;
    envelope.set_request_id(requestId);
    envelope.set_is_response(true);
    envelope.set_method(qodex::threadui::ipc::kLoginMethodName);
    envelope.set_payload(response.SerializeAsString());
    return envelope;
}

bool sendEnvelope(
    QTcpSocket *socket,
    const qodex::threadui::ipc::common::RpcEnvelope &envelope
) {
    if (socket == nullptr) {
        return false;
    }

    const std::string frame = qodex::threadui::ipc::encodeEnvelopeFrame(envelope);
    const bool writeQueued = socket->write(frame.data(), static_cast<qint64>(frame.size())) >= 0;
    socket->flush();
    return writeQueued;
}

}  // namespace

ThreadUiIpcServer::ThreadUiIpcServer(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this)) {
    Q_ASSERT(m_server != nullptr);
    QObject::connect(m_server, &QTcpServer::newConnection, this, &ThreadUiIpcServer::onNewConnection);
}

ThreadUiIpcServer::~ThreadUiIpcServer() {
    const auto connections = m_connectionStates.keys();
    for (QTcpSocket *socket : connections) {
        if (socket != nullptr) {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }

    m_connectionStates.clear();
    m_unauthenticatedConnections.clear();
    m_authenticatedConnectionsByToken.clear();
    m_issuedLaunchTokens.clear();

    if (m_server != nullptr) {
        m_server->close();
    }
}

bool ThreadUiIpcServer::listen(QString *errorMessage) {
    if (m_server == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Thread UI IPC server is not available.");
        }
        return false;
    }

    if (m_server->isListening()) {
        return true;
    }

    if (!m_server->listen(QHostAddress(QHostAddress::LocalHost), 0)) {
        if (errorMessage != nullptr) {
            *errorMessage = m_server->errorString();
        }
        return false;
    }

    return true;
}

bool ThreadUiIpcServer::isListening() const {
    return m_server != nullptr && m_server->isListening();
}

QString ThreadUiIpcServer::host() const {
    return QHostAddress(QHostAddress::LocalHost).toString();
}

quint16 ThreadUiIpcServer::port() const {
    return m_server != nullptr ? m_server->serverPort() : 0;
}

ThreadUiLaunchConfig ThreadUiIpcServer::allocateLaunchConfig() {
    QString token;
    do {
        token = generateLaunchToken();
    } while (m_issuedLaunchTokens.contains(token));

    m_issuedLaunchTokens.insert(token);
    return ThreadUiLaunchConfig{
        .host = host(),
        .port = port(),
        .token = token,
    };
}

int ThreadUiIpcServer::unauthenticatedConnectionCount() const {
    return m_unauthenticatedConnections.size();
}

int ThreadUiIpcServer::authenticatedConnectionCount() const {
    return m_authenticatedConnectionsByToken.size();
}

void ThreadUiIpcServer::onNewConnection() {
    if (m_server == nullptr) {
        return;
    }

    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }

        socket->setParent(this);
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_connectionStates.insert(socket, ConnectionState{});
        m_unauthenticatedConnections.insert(socket);

        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            onSocketReadyRead(socket);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            removeConnection(socket);
            socket->deleteLater();
        });
        QObject::connect(socket, &QObject::destroyed, this, [this, socket] {
            removeConnection(socket);
        });
    }
}

void ThreadUiIpcServer::onSocketReadyRead(QTcpSocket *socket) {
    if (socket == nullptr) {
        return;
    }

    auto connectionStateIt = m_connectionStates.find(socket);
    if (connectionStateIt == m_connectionStates.end()) {
        return;
    }

    const QByteArray chunk = socket->readAll();
    connectionStateIt->inputBuffer.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));

    while (true) {
        qodex::threadui::ipc::common::RpcEnvelope envelope;
        std::string parseErrorMessage;
        const FrameDecodeResult decodeResult =
            qodex::threadui::ipc::tryDecodeNextEnvelope(&connectionStateIt->inputBuffer, &envelope, &parseErrorMessage);
        if (decodeResult == FrameDecodeResult::Incomplete) {
            return;
        }

        if (decodeResult == FrameDecodeResult::InvalidFrame) {
            qWarning("Thread UI IPC protocol error: %s", parseErrorMessage.c_str());
            socket->disconnectFromHost();
            return;
        }

        if (!connectionStateIt->authenticatedToken.isEmpty()) {
            qWarning("Received an unexpected authenticated Thread UI IPC message before handlers exist.");
            continue;
        }

        if (envelope.is_response() || envelope.method() != qodex::threadui::ipc::kLoginMethodName) {
            sendEnvelope(
                socket,
                makeLoginResponseEnvelope(
                    envelope.request_id(),
                    RESULT_STATUS_ERROR,
                    QStringLiteral("Expected UiToQodex.Login as the first request.")
                )
            );
            socket->disconnectFromHost();
            return;
        }

        qodex::threadui::ipc::ui_to_qodex::LoginRequest request;
        if (!request.ParseFromString(envelope.payload())) {
            sendEnvelope(
                socket,
                makeLoginResponseEnvelope(
                    envelope.request_id(),
                    RESULT_STATUS_ERROR,
                    QStringLiteral("Failed to parse UiToQodex.Login request payload.")
                )
            );
            socket->disconnectFromHost();
            return;
        }

        const QString token = QString::fromStdString(request.token());
        if (!m_issuedLaunchTokens.contains(token)) {
            sendEnvelope(
                socket,
                makeLoginResponseEnvelope(
                    envelope.request_id(),
                    RESULT_STATUS_ERROR,
                    QStringLiteral("Login token was not recognized.")
                )
            );
            socket->disconnectFromHost();
            return;
        }

        if (m_authenticatedConnectionsByToken.contains(token)) {
            sendEnvelope(
                socket,
                makeLoginResponseEnvelope(
                    envelope.request_id(),
                    RESULT_STATUS_ERROR,
                    QStringLiteral("Login token is already in use by another connection.")
                )
            );
            socket->disconnectFromHost();
            return;
        }

        connectionStateIt->authenticatedToken = token;
        m_unauthenticatedConnections.remove(socket);
        m_authenticatedConnectionsByToken.insert(token, socket);

        sendEnvelope(
            socket,
            makeLoginResponseEnvelope(
                envelope.request_id(),
                RESULT_STATUS_OK,
                QStringLiteral("Thread UI connection authenticated.")
            )
        );
    }
}

void ThreadUiIpcServer::removeConnection(QTcpSocket *socket) {
    if (socket == nullptr) {
        return;
    }

    const auto connectionStateIt = m_connectionStates.find(socket);
    if (connectionStateIt != m_connectionStates.end()) {
        if (!connectionStateIt->authenticatedToken.isEmpty()) {
            m_authenticatedConnectionsByToken.remove(connectionStateIt->authenticatedToken);
        }
        m_connectionStates.erase(connectionStateIt);
    }

    m_unauthenticatedConnections.remove(socket);
}

QString ThreadUiIpcServer::generateLaunchToken() {
    const quint32 a = QRandomGenerator::system()->generate();
    const quint32 b = QRandomGenerator::system()->generate();
    return QStringLiteral("%1%2")
        .arg(a, 8, 16, QLatin1Char('0'))
        .arg(b, 8, 16, QLatin1Char('0'));
}

}  // namespace qodex::threadui
