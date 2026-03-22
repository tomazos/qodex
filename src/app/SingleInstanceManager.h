#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLockFile;

namespace qodex::app {

class SingleInstanceManager final : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceManager(const QString &serverName = {}, QObject *parent = nullptr);
    ~SingleInstanceManager() override;

    [[nodiscard]] bool startPrimaryOrActivateExisting(int timeoutMs = 1000);
    [[nodiscard]] bool isPrimary() const;
    bool takePendingActivation();

signals:
    void activationRequested();

private:
    [[nodiscard]] QString resolvedLockFilePath() const;
    [[nodiscard]] QString resolvedServerName() const;
    [[nodiscard]] bool tryActivateExisting(int timeoutMs) const;
    void recordActivationRequest();
    void onNewConnection();

    QString m_serverName;
    QLockFile *m_lockFile = nullptr;
    QLocalServer *m_server = nullptr;
    bool m_isPrimary = false;
    bool m_hasPendingActivation = false;
};

}  // namespace qodex::app
