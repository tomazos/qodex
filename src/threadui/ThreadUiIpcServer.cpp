#include "threadui/ThreadUiIpcServer.h"

#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>

namespace qodex::threadui {

ThreadUiIpcServer::ThreadUiIpcServer(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this)) {
    Q_ASSERT(m_server != nullptr);
}

ThreadUiIpcServer::~ThreadUiIpcServer() {
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

ThreadUiLaunchConfig ThreadUiIpcServer::allocateLaunchConfig() const {
    return ThreadUiLaunchConfig{
        .host = host(),
        .port = port(),
        .token = generateLaunchToken(),
    };
}

QString ThreadUiIpcServer::generateLaunchToken() {
    const quint32 a = QRandomGenerator::system()->generate();
    const quint32 b = QRandomGenerator::system()->generate();
    return QStringLiteral("%1%2")
        .arg(a, 8, 16, QLatin1Char('0'))
        .arg(b, 8, 16, QLatin1Char('0'));
}

}  // namespace qodex::threadui
