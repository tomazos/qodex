#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QList>
#include <QObject>
#include <QString>

#include "app/ThreadUiProcess.h"

namespace qodex::threadui {
class ThreadUiIpcServer;
}

namespace qodex::app {

class ThreadUiProcessManager final : public QObject {
    Q_OBJECT

public:
    explicit ThreadUiProcessManager(qodex::threadui::ThreadUiIpcServer *threadUiIpcServer, QObject *parent = nullptr);
    ~ThreadUiProcessManager() override;

    [[nodiscard]] QList<ThreadUiProcessInfo> activeProcesses() const;
    [[nodiscard]] ThreadUiProcess *launchThreadUiForThread(const QString &threadId, const QString &title);
    void destroyThreadUiForThread(const QString &threadId);
    [[nodiscard]] ThreadUiProcess *threadUiProcessForThread(const QString &threadId) const;
    [[nodiscard]] bool activateThreadUiForThread(const QString &threadId, QString *errorMessage = nullptr);

public slots:
    void activateThreadUi(int instanceId);

signals:
    void activeProcessesChanged();
    void statusMessageRequested(const QString &message);
    void threadUiProcessExited(const QString &threadId);

private:
    [[nodiscard]] static QString resolveThreadUiAppDir();
    [[nodiscard]] static QString resolveThreadUiStartScriptPath(const QString &appDir);
    [[nodiscard]] static QString resolveNodeProgram();
    [[nodiscard]] ThreadUiProcess *processForInstanceId(int instanceId) const;
    [[nodiscard]] ThreadUiProcess *processForLaunchToken(const QString &launchToken) const;
    [[nodiscard]] ThreadUiProcess *createProcess(const QString &threadId, const QString &title);
    void removeProcess(const QString &threadId);
    void terminateAllProcesses();
    void onThreadUiAuthenticated(const QString &token);
    void onThreadUiDisconnected(const QString &token);
    void onSendUserInputRequested(const QString &token, std::uint64_t requestId, const QString &text);

private:
    QString m_threadUiAppDir;
    QString m_threadUiStartScriptPath;
    QString m_nodeProgram;
    qodex::threadui::ThreadUiIpcServer *m_threadUiIpcServer = nullptr;
    std::vector<std::unique_ptr<ThreadUiProcess>> m_processes;
    int m_nextInstanceId = 1;
};

}  // namespace qodex::app
