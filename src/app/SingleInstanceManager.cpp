#include "app/SingleInstanceManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QStandardPaths>

namespace qodex::app {

SingleInstanceManager::SingleInstanceManager(const QString &scopeKey, QObject *parent)
    : QObject(parent),
      m_scopeKey(scopeKey),
      m_lockFile(new QLockFile(resolvedLockFilePath())),
      m_server(new QLocalServer(this)) {
    Q_ASSERT(m_lockFile != nullptr);
    Q_ASSERT(m_server != nullptr);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceManager::onNewConnection);
}

SingleInstanceManager::~SingleInstanceManager() {
    if (m_server != nullptr) {
        m_server->close();
    }
    delete m_lockFile;
    if (m_isPrimary) {
        QLocalServer::removeServer(resolvedServerName());
    }
}

bool SingleInstanceManager::startPrimaryOrActivateExisting(const int timeoutMs) {
    if (m_isPrimary) {
        return true;
    }

    Q_ASSERT(m_lockFile != nullptr);
    const QString serverName = resolvedServerName();
    if (!m_lockFile->tryLock(0)) {
        if (tryActivateExisting(timeoutMs)) {
            return false;
        }
        if (m_lockFile->removeStaleLockFile() && m_lockFile->tryLock(0)) {
            // Lock recovered after a stale/crashed instance.
        } else {
            return false;
        }
    }

    QLocalServer::removeServer(serverName);
    if (m_server != nullptr && m_server->listen(serverName)) {
        m_isPrimary = true;
        return true;
    }
    return true;
}

bool SingleInstanceManager::isPrimary() const {
    return m_isPrimary;
}

bool SingleInstanceManager::takePendingActivation() {
    const bool hadPendingActivation = m_hasPendingActivation;
    m_hasPendingActivation = false;
    return hadPendingActivation;
}

QString SingleInstanceManager::resolvedServerName() const {
    const QString keySource = m_scopeKey.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : m_scopeKey;
    const QString organization = QCoreApplication::organizationName().isEmpty()
        ? QStringLiteral("qodex")
        : QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("qodex")
        : QCoreApplication::applicationName();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(keySource.toUtf8(), QCryptographicHash::Sha256).toHex().first(12)
    );
    auto sanitize = [](QString value) {
        value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        return value;
    };

    return QStringLiteral("%1.%2.%3.single-instance")
        .arg(sanitize(organization), sanitize(application), hash);
}

QString SingleInstanceManager::resolvedLockFilePath() const {
    QString stateLocation = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (stateLocation.isEmpty()) {
        stateLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (stateLocation.isEmpty()) {
        stateLocation = QDir::tempPath();
    }

    QDir stateDir(stateLocation);
    stateDir.mkpath(QStringLiteral("."));
    return stateDir.filePath(resolvedServerName() + QStringLiteral(".lock"));
}

bool SingleInstanceManager::tryActivateExisting(const int timeoutMs) const {
    QLocalSocket socket;
    socket.connectToServer(resolvedServerName());
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(timeoutMs);
    }
    return true;
}

void SingleInstanceManager::recordActivationRequest() {
    m_hasPendingActivation = true;
    emit activationRequested();
}

void SingleInstanceManager::onNewConnection() {
    if (m_server == nullptr) {
        return;
    }

    while (m_server->hasPendingConnections()) {
        if (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            socket->disconnectFromServer();
        }
        recordActivationRequest();
    }
}

}  // namespace qodex::app
