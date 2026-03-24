#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <string>
#include <unordered_map>

class QTcpServer;
class QTcpSocket;

namespace qodex::threadui {

struct ThreadUiLaunchConfig {
    QString host;
    quint16 port = 0;
    QString token;
};

class ThreadUiIpcServer final : public QObject {
    Q_OBJECT

public:
    explicit ThreadUiIpcServer(QObject *parent = nullptr);
    ~ThreadUiIpcServer() override;

    [[nodiscard]] bool listen(QString *errorMessage = nullptr);
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString host() const;
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] ThreadUiLaunchConfig allocateLaunchConfig();
    [[nodiscard]] int unauthenticatedConnectionCount() const;
    [[nodiscard]] int authenticatedConnectionCount() const;
    [[nodiscard]] std::int64_t highestReceivedTestPing() const;

private:
    struct ConnectionState {
        std::string inputBuffer;
        QString authenticatedToken;
        std::uint64_t nextOutgoingRequestId = 1;
        std::unordered_map<std::uint64_t, std::int64_t> pendingTestPongValuesByRequestId;
    };

    void onNewConnection();
    void onSocketReadyRead(QTcpSocket *socket);
    void removeConnection(QTcpSocket *socket);
    static QString generateLaunchToken();

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, ConnectionState> m_connectionStates;
    QSet<QTcpSocket *> m_unauthenticatedConnections;
    QSet<QString> m_issuedLaunchTokens;
    QHash<QString, QTcpSocket *> m_authenticatedConnectionsByToken;
    std::int64_t m_highestReceivedTestPing = 0;
};

}  // namespace qodex::threadui
