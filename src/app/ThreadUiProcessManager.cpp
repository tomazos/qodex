#include "app/ThreadUiProcessManager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "threadui/ThreadUiIpcServer.h"

namespace qodex::app {

namespace {

QString formatProcessMenuTitle(const ThreadUiProcessInfo &info) {
    if (info.processId > 0) {
        return QStringLiteral("%1 (PID %2)").arg(info.title).arg(info.processId);
    }

    return info.title;
}

}  // namespace

ThreadUiProcessManager::ThreadUiProcessManager(qodex::threadui::ThreadUiIpcServer *threadUiIpcServer, QObject *parent)
    : QObject(parent),
      m_threadUiAppDir(resolveThreadUiAppDir()),
      m_threadUiStartScriptPath(resolveThreadUiStartScriptPath(m_threadUiAppDir)),
      m_nodeProgram(resolveNodeProgram()),
      m_threadUiIpcServer(threadUiIpcServer) {
    Q_ASSERT(m_threadUiIpcServer != nullptr);
    QObject::connect(
        m_threadUiIpcServer,
        &qodex::threadui::ThreadUiIpcServer::threadUiAuthenticated,
        this,
        &ThreadUiProcessManager::onThreadUiAuthenticated
    );
    QObject::connect(
        m_threadUiIpcServer,
        &qodex::threadui::ThreadUiIpcServer::threadUiDisconnected,
        this,
        &ThreadUiProcessManager::onThreadUiDisconnected
    );
    QObject::connect(
        m_threadUiIpcServer,
        &qodex::threadui::ThreadUiIpcServer::sendUserInputRequested,
        this,
        &ThreadUiProcessManager::onSendUserInputRequested
    );
}

ThreadUiProcessManager::~ThreadUiProcessManager() {
    terminateAllProcesses();
}

QList<ThreadUiProcessInfo> ThreadUiProcessManager::activeProcesses() const {
    QList<ThreadUiProcessInfo> processes;
    processes.reserve(static_cast<qsizetype>(m_processes.size()));

    for (const std::unique_ptr<ThreadUiProcess> &process : m_processes) {
        if (process == nullptr || !process->isRunning()) {
            continue;
        }

        ThreadUiProcessInfo info = process->info();
        info.title = formatProcessMenuTitle(info);
        processes.append(info);
    }

    return processes;
}

ThreadUiProcess *ThreadUiProcessManager::launchThreadUiForThread(const QString &threadId, const QString &title) {
    if (threadId.trimmed().isEmpty()) {
        emit statusMessageRequested(QStringLiteral("Unable to launch Thread UI: thread id is empty."));
        return nullptr;
    }

    ThreadUiProcess *process = threadUiProcessForThread(threadId);
    if (process == nullptr) {
        process = createProcess(threadId, title);
    }

    if (process == nullptr) {
        return nullptr;
    }

    process->relaunch(title);
    return process;
}

void ThreadUiProcessManager::destroyThreadUiForThread(const QString &threadId) {
    removeProcess(threadId);
}

ThreadUiProcess *ThreadUiProcessManager::threadUiProcessForThread(const QString &threadId) const {
    const auto it = std::find_if(
        m_processes.cbegin(),
        m_processes.cend(),
        [&threadId](const std::unique_ptr<ThreadUiProcess> &process) {
            return process != nullptr && process->threadId() == threadId;
        }
    );
    return it != m_processes.cend() ? it->get() : nullptr;
}

void ThreadUiProcessManager::activateThreadUi(const int instanceId) {
    ThreadUiProcess *process = processForInstanceId(instanceId);
    if (process == nullptr) {
        emit statusMessageRequested(QStringLiteral("That Thread UI subprocess is no longer running."));
        return;
    }

    QString errorMessage;
    if (!process->activate(&errorMessage)) {
        emit statusMessageRequested(errorMessage);
        return;
    }

    emit statusMessageRequested(QStringLiteral("Activating %1...").arg(process->title()));
}

QString ThreadUiProcessManager::resolveThreadUiAppDir() {
    const QString environmentOverride = QProcessEnvironment::systemEnvironment()
                                            .value(QStringLiteral("QODEX_THREAD_UI_APP_DIR"))
                                            .trimmed();
    if (!environmentOverride.isEmpty()) {
        return QDir::cleanPath(environmentOverride);
    }

#ifdef QODEX_THREAD_UI_BUILD_APP_DIR
    const QString configuredBuildPath = QString::fromLocal8Bit(QODEX_THREAD_UI_BUILD_APP_DIR);
    if (!configuredBuildPath.isEmpty() && QFileInfo::exists(configuredBuildPath)) {
        return QDir::cleanPath(configuredBuildPath);
    }
#endif

#ifdef QODEX_THREAD_UI_INSTALLED_RELATIVE_APP_DIR
    const QString installedRelativePath = QString::fromLocal8Bit(QODEX_THREAD_UI_INSTALLED_RELATIVE_APP_DIR);
    if (!installedRelativePath.isEmpty()) {
        const QString installedCandidate =
            QDir(QCoreApplication::applicationDirPath()).filePath(installedRelativePath);
        if (QFileInfo::exists(installedCandidate)) {
            return QDir::cleanPath(installedCandidate);
        }
    }
#endif

    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime/thread-ui/app"));
}

QString ThreadUiProcessManager::resolveThreadUiStartScriptPath(const QString &appDir) {
    return QDir(appDir).filePath(QStringLiteral("scripts/start-electron.js"));
}

QString ThreadUiProcessManager::resolveNodeProgram() {
    QString nodeProgram = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (nodeProgram.isEmpty()) {
        nodeProgram = QStandardPaths::findExecutable(QStringLiteral("nodejs"));
    }
    return nodeProgram;
}

ThreadUiProcess *ThreadUiProcessManager::processForInstanceId(const int instanceId) const {
    const auto it = std::find_if(
        m_processes.cbegin(),
        m_processes.cend(),
        [instanceId](const std::unique_ptr<ThreadUiProcess> &process) {
            return process != nullptr && process->instanceId() == instanceId;
        }
    );
    return it != m_processes.cend() ? it->get() : nullptr;
}

ThreadUiProcess *ThreadUiProcessManager::processForLaunchToken(const QString &launchToken) const {
    const auto it = std::find_if(
        m_processes.cbegin(),
        m_processes.cend(),
        [&launchToken](const std::unique_ptr<ThreadUiProcess> &process) {
            return process != nullptr && process->matchesLaunchToken(launchToken);
        }
    );
    return it != m_processes.cend() ? it->get() : nullptr;
}

ThreadUiProcess *ThreadUiProcessManager::createProcess(const QString &threadId, const QString &title) {
    auto process = std::make_unique<ThreadUiProcess>(
        m_nextInstanceId++,
        threadId,
        title,
        m_threadUiAppDir,
        m_threadUiStartScriptPath,
        m_nodeProgram,
        m_threadUiIpcServer,
        this
    );

    ThreadUiProcess *processPtr = process.get();
    QObject::connect(processPtr, &ThreadUiProcess::activeStateChanged, this, [this] {
        emit activeProcessesChanged();
    });
    QObject::connect(processPtr, &ThreadUiProcess::statusMessageRequested, this, [this](const QString &message) {
        emit statusMessageRequested(message);
    });
    QObject::connect(processPtr, &ThreadUiProcess::processExitedExternally, this, [this](const QString &threadId) {
        emit threadUiProcessExited(threadId);
    });

    m_processes.push_back(std::move(process));
    return processPtr;
}

void ThreadUiProcessManager::removeProcess(const QString &threadId) {
    const auto it = std::find_if(
        m_processes.begin(),
        m_processes.end(),
        [&threadId](const std::unique_ptr<ThreadUiProcess> &process) {
            return process != nullptr && process->threadId() == threadId;
        }
    );
    if (it == m_processes.end()) {
        return;
    }

    (*it)->terminate();
    m_processes.erase(it);
    emit activeProcessesChanged();
}

void ThreadUiProcessManager::terminateAllProcesses() {
    for (const std::unique_ptr<ThreadUiProcess> &process : m_processes) {
        if (process != nullptr) {
            process->terminate();
        }
    }

    m_processes.clear();
    emit activeProcessesChanged();
}

void ThreadUiProcessManager::onThreadUiAuthenticated(const QString &token) {
    ThreadUiProcess *process = processForLaunchToken(token);
    if (process == nullptr) {
        return;
    }

    process->handleAuthenticated();
}

void ThreadUiProcessManager::onThreadUiDisconnected(const QString &token) {
    ThreadUiProcess *process = processForLaunchToken(token);
    if (process == nullptr) {
        return;
    }

    process->handleDisconnected();
}

void ThreadUiProcessManager::onSendUserInputRequested(
    const QString &token,
    const std::uint64_t requestId,
    const QString &text
) {
    ThreadUiProcess *process = processForLaunchToken(token);
    if (process == nullptr) {
        return;
    }

    process->handleUserInputRequested(requestId, text);
}

}  // namespace qodex::app
