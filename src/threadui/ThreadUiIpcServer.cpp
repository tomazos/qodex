#include "threadui/ThreadUiIpcServer.h"

#include <algorithm>
#include <QByteArray>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.qodex_rpc.h"

namespace qodex::threadui {

namespace {

using qodex::threadui::ipc::FrameDecodeResult;
using qodex::threadui::ipc::common::RESULT_STATUS_ERROR;
using qodex::threadui::ipc::common::RESULT_STATUS_OK;
namespace UiToQodexRpc = qodex::threadui::ipc::ui_to_qodex::rpc::UiToQodex;

qodex::threadui::ipc::common::RpcEnvelope makeResponseEnvelope(
    const std::uint64_t requestId,
    const qodex::threadui::ipc::ui_to_qodex::LoginResponse &response
) {
    return qodex::threadui::ipc::makeResponseEnvelope<UiToQodexRpc::Login>(requestId, response);
}

qodex::threadui::ipc::common::RpcEnvelope makeLoginResponseEnvelope(
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const QString &message
) {
    qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
    response.set_status(status);
    response.set_message(message.toStdString());

    return makeResponseEnvelope(requestId, response);
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

        auto &connectionState = connectionStateIt.value();

        if (!connectionStateIt->authenticatedToken.isEmpty()) {
            struct UiToQodexRequestHandler final {
                QTcpSocket *socket;

                bool onLoginRequest(
                    const std::uint64_t requestId,
                    const qodex::threadui::ipc::ui_to_qodex::LoginRequest &,
                    std::string *errorMessage
                ) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "UiToQodex.Login is only valid before authentication.";
                    }

                    sendEnvelope(
                        socket,
                        makeLoginResponseEnvelope(
                            requestId,
                            RESULT_STATUS_ERROR,
                            QStringLiteral("UiToQodex.Login is only valid before authentication.")
                        )
                    );
                    return false;
                }
            } handler{socket};

            std::string dispatchErrorMessage;
            if (!UiToQodexRpc::dispatchRequestEnvelope(envelope, handler, &dispatchErrorMessage)) {
                qWarning("Thread UI IPC protocol error: %s", dispatchErrorMessage.c_str());
                socket->disconnectFromHost();
                return;
            }
            continue;
        }

        struct LoginRequestHandler final {
            QTcpSocket *socket;
            ConnectionState &connectionState;
            QSet<QString> &issuedLaunchTokens;
            QHash<QString, QTcpSocket *> &authenticatedConnectionsByToken;
            QSet<QTcpSocket *> &unauthenticatedConnections;

            bool onLoginRequest(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::LoginRequest &request,
                std::string *errorMessage
            ) {
                const QString token = QString::fromStdString(request.token());
                if (!issuedLaunchTokens.contains(token)) {
                    sendEnvelope(
                        socket,
                        makeLoginResponseEnvelope(
                            requestId,
                            RESULT_STATUS_ERROR,
                            QStringLiteral("Login token was not recognized.")
                        )
                    );
                    if (errorMessage != nullptr) {
                        *errorMessage = "Login token was not recognized.";
                    }
                    return false;
                }

                if (authenticatedConnectionsByToken.contains(token)) {
                    sendEnvelope(
                        socket,
                        makeLoginResponseEnvelope(
                            requestId,
                            RESULT_STATUS_ERROR,
                            QStringLiteral("Login token is already in use by another connection.")
                        )
                    );
                    if (errorMessage != nullptr) {
                        *errorMessage = "Login token is already in use by another connection.";
                    }
                    return false;
                }

                connectionState.authenticatedToken = token;
                unauthenticatedConnections.remove(socket);
                authenticatedConnectionsByToken.insert(token, socket);

                if (!sendEnvelope(
                        socket,
                        makeLoginResponseEnvelope(
                            requestId,
                            RESULT_STATUS_OK,
                            QStringLiteral("Thread UI connection authenticated.")
                        ))) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Failed to send UiToQodex.Login response.";
                    }
                    return false;
                }

                return true;
            }
        } handler{
            socket,
            connectionState,
            m_issuedLaunchTokens,
            m_authenticatedConnectionsByToken,
            m_unauthenticatedConnections,
        };

        std::string dispatchErrorMessage;
        if (!UiToQodexRpc::dispatchRequestEnvelope(envelope, handler, &dispatchErrorMessage)) {
            qWarning("Thread UI IPC protocol error: %s", dispatchErrorMessage.c_str());
            socket->disconnectFromHost();
            return;
        }
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
