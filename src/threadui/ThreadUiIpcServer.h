#pragma once

#include <QObject>
#include <QString>

class QTcpServer;

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
    [[nodiscard]] ThreadUiLaunchConfig allocateLaunchConfig() const;

private:
    static QString generateLaunchToken();

    QTcpServer *m_server = nullptr;
};

}  // namespace qodex::threadui
