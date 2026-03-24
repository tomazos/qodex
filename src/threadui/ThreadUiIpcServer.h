#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <string>

#include "common.pb.h"
#include "qodex_to_ui.pb.h"

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
    [[nodiscard]] bool sendAddItems(
        const QString &token,
        const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request,
        QString *errorMessage = nullptr
    );
    [[nodiscard]] bool sendUserInputResponse(
        const QString &token,
        std::uint64_t requestId,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message,
        QString *errorMessage = nullptr
    );

signals:
    void threadUiAuthenticated(const QString &token);
    void threadUiDisconnected(const QString &token);
    void sendUserInputRequested(const QString &token, std::uint64_t requestId, const QString &text);

private:
    struct ConnectionState {
        std::string inputBuffer;
        QString authenticatedToken;
        std::uint64_t nextOutgoingRequestId = 1;
        QSet<quint64> pendingAddItemsRequestIds;
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
};

}  // namespace qodex::threadui
