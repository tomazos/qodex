#include "app/ThreadUiProcessManager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "qodex_to_ui.pb.h"
#include "threadui/ThreadUiIpcServer.h"

namespace qodex::app {

namespace {

QString formatProcessMenuTitle(const ThreadUiProcessInfo &info) {
    if (info.processId > 0) {
        return QStringLiteral("%1 (PID %2)").arg(info.title).arg(info.processId);
    }

    return info.title;
}

QString appendStderrMessage(const QString &baseMessage, const QString &stderrText) {
    const QString trimmedStderr = stderrText.trimmed();
    if (trimmedStderr.isEmpty()) {
        return baseMessage;
    }

    return QStringLiteral("%1 %2").arg(baseMessage, trimmedStderr);
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
}

ThreadUiProcessManager::~ThreadUiProcessManager() {
    terminateAllProcesses();
}

QList<ThreadUiProcessInfo> ThreadUiProcessManager::activeProcesses() const {
    QList<ThreadUiProcessInfo> processes;
    processes.reserve(static_cast<qsizetype>(m_processes.size()));

    for (const std::unique_ptr<ProcessRecord> &record : m_processes) {
        if (record == nullptr || record->process == nullptr || record->process->state() == QProcess::NotRunning) {
            continue;
        }

        const ThreadUiProcessInfo info{
            .instanceId = record->instanceId,
            .title = record->title,
            .processId = record->process->processId(),
        };
        processes.append({
            .instanceId = info.instanceId,
            .title = formatProcessMenuTitle(info),
            .processId = info.processId,
        });
    }

    return processes;
}

void ThreadUiProcessManager::showResumedThread(
    const QString &threadId,
    const QString &title,
    const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &addItemsRequest
) {
    if (threadId.trimmed().isEmpty()) {
        emit statusMessageRequested(QStringLiteral("Unable to resume thread: thread id is empty."));
        return;
    }

    ProcessRecord *existingRecord = recordForThreadId(threadId);
    if (existingRecord != nullptr) {
        terminateRecord(existingRecord);
    }

    relaunchThreadUiForThread(threadId, title);

    ProcessRecord *record = recordForThreadId(threadId);
    if (record == nullptr) {
        return;
    }

    record->pendingAddItemsRequest = addItemsRequest;
    flushPendingAddItems(record);
}

void ThreadUiProcessManager::relaunchThreadUiForThread(const QString &threadId, const QString &title) {
    if (m_threadUiIpcServer == nullptr || !m_threadUiIpcServer->isListening()) {
        emit statusMessageRequested(QStringLiteral("Unable to launch Thread UI: IPC server is not listening."));
        return;
    }

    if (m_nodeProgram.isEmpty()) {
        emit statusMessageRequested(QStringLiteral("Unable to launch Thread UI: node executable not found."));
        return;
    }

    if (!QFileInfo::exists(m_threadUiStartScriptPath)) {
        emit statusMessageRequested(
            QStringLiteral("Unable to launch Thread UI: %1 is missing. Build qodex_thread_ui first.")
                .arg(m_threadUiStartScriptPath)
        );
        return;
    }

    const int instanceId = m_nextInstanceId++;
    const QString displayTitle = title.trimmed().isEmpty() ? threadId : title.trimmed();
    const qodex::threadui::ThreadUiLaunchConfig launchConfig = m_threadUiIpcServer->allocateLaunchConfig();

    auto record = std::make_unique<ProcessRecord>();
    record->instanceId = instanceId;
    record->threadId = threadId;
    record->title = displayTitle;
    record->launchToken = launchConfig.token;
    record->process = new QProcess(this);
    record->process->setWorkingDirectory(m_threadUiAppDir);
    record->process->setProgram(m_nodeProgram);
    record->process->setArguments({
        m_threadUiStartScriptPath,
        QStringLiteral("--qodex-title=%1").arg(displayTitle),
        QStringLiteral("--qodex-ipc-host=%1").arg(launchConfig.host),
        QStringLiteral("--qodex-ipc-port=%1").arg(launchConfig.port),
        QStringLiteral("--qodex-ipc-token=%1").arg(launchConfig.token),
    });

    QObject::connect(record->process, &QProcess::started, this, [this] {
        emit activeProcessesChanged();
    });
    QObject::connect(record->process, &QProcess::readyReadStandardOutput, this, [this, instanceId] {
        drainStandardOutput(recordForInstanceId(instanceId));
    });
    QObject::connect(record->process, &QProcess::readyReadStandardError, this, [this, instanceId] {
        drainStandardError(recordForInstanceId(instanceId));
    });
    QObject::connect(
        record->process,
        &QProcess::errorOccurred,
        this,
        [this, instanceId](const QProcess::ProcessError error) {
            const ProcessRecord *record = recordForInstanceId(instanceId);
            const QString baseMessage = error == QProcess::FailedToStart
                ? QStringLiteral("Failed to start %1.").arg(record != nullptr ? record->title : QStringLiteral("Thread UI"))
                : QStringLiteral("%1 reported a process error.").arg(record != nullptr ? record->title : QStringLiteral("Thread UI"));
            emit statusMessageRequested(
                appendStderrMessage(baseMessage, record != nullptr ? record->lastStandardError : QString{})
            );
            removeRecord(instanceId);
        }
    );
    QObject::connect(
        record->process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, instanceId](const int exitCode, const QProcess::ExitStatus exitStatus) {
            const ProcessRecord *record = recordForInstanceId(instanceId);
            if (exitStatus == QProcess::CrashExit) {
                emit statusMessageRequested(
                    appendStderrMessage(
                        QStringLiteral("%1 crashed with exit code %2.")
                            .arg(record != nullptr ? record->title : QStringLiteral("Thread UI"))
                            .arg(exitCode),
                        record != nullptr ? record->lastStandardError : QString{}
                    )
                );
            }
            removeRecord(instanceId);
        }
    );

    m_processes.push_back(std::move(record));
    m_processes.back()->process->start();
    emit activeProcessesChanged();
    emit statusMessageRequested(QStringLiteral("Launching %1...").arg(displayTitle));
}

void ThreadUiProcessManager::activateThreadUi(const int instanceId) {
    ProcessRecord *record = recordForInstanceId(instanceId);
    if (record == nullptr || record->process == nullptr || record->process->state() == QProcess::NotRunning) {
        removeRecord(instanceId);
        emit statusMessageRequested(QStringLiteral("That Thread UI subprocess is no longer running."));
        return;
    }

    const qint64 bytesWritten = record->process->write("activate\n");
    if (bytesWritten <= 0) {
        emit statusMessageRequested(QStringLiteral("Failed to activate %1.").arg(record->title));
        return;
    }

    emit statusMessageRequested(QStringLiteral("Activating %1...").arg(record->title));
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

ThreadUiProcessManager::ProcessRecord *ThreadUiProcessManager::recordForInstanceId(const int instanceId) {
    const auto it = std::find_if(
        m_processes.begin(),
        m_processes.end(),
        [instanceId](const std::unique_ptr<ProcessRecord> &record) {
            return record != nullptr && record->instanceId == instanceId;
        }
    );
    return it != m_processes.end() ? it->get() : nullptr;
}

const ThreadUiProcessManager::ProcessRecord *ThreadUiProcessManager::recordForInstanceId(const int instanceId) const {
    const auto it = std::find_if(
        m_processes.cbegin(),
        m_processes.cend(),
        [instanceId](const std::unique_ptr<ProcessRecord> &record) {
            return record != nullptr && record->instanceId == instanceId;
        }
    );
    return it != m_processes.cend() ? it->get() : nullptr;
}

ThreadUiProcessManager::ProcessRecord *ThreadUiProcessManager::recordForThreadId(const QString &threadId) {
    const auto it = std::find_if(
        m_processes.begin(),
        m_processes.end(),
        [&threadId](const std::unique_ptr<ProcessRecord> &record) {
            return record != nullptr && record->threadId == threadId;
        }
    );
    return it != m_processes.end() ? it->get() : nullptr;
}

ThreadUiProcessManager::ProcessRecord *ThreadUiProcessManager::recordForLaunchToken(const QString &launchToken) {
    const auto it = std::find_if(
        m_processes.begin(),
        m_processes.end(),
        [&launchToken](const std::unique_ptr<ProcessRecord> &record) {
            return record != nullptr && record->launchToken == launchToken;
        }
    );
    return it != m_processes.end() ? it->get() : nullptr;
}

void ThreadUiProcessManager::flushPendingAddItems(ProcessRecord *record) {
    if (record == nullptr || !record->authenticated || !record->pendingAddItemsRequest.has_value()) {
        return;
    }

    QString errorMessage;
    if (!m_threadUiIpcServer->sendAddItems(record->launchToken, *record->pendingAddItemsRequest, &errorMessage)) {
        emit statusMessageRequested(
            QStringLiteral("Failed to deliver thread history to %1: %2").arg(record->title, errorMessage)
        );
        return;
    }

    record->pendingAddItemsRequest.reset();
}

void ThreadUiProcessManager::removeRecord(const int instanceId) {
    const auto it = std::find_if(
        m_processes.begin(),
        m_processes.end(),
        [instanceId](const std::unique_ptr<ProcessRecord> &record) {
            return record != nullptr && record->instanceId == instanceId;
        }
    );
    if (it == m_processes.end()) {
        return;
    }

    if ((*it)->process != nullptr) {
        (*it)->process->deleteLater();
    }

    m_processes.erase(it);
    emit activeProcessesChanged();
}

void ThreadUiProcessManager::terminateRecord(ProcessRecord *record) {
    if (record == nullptr) {
        return;
    }

    const int instanceId = record->instanceId;
    if (record->process != nullptr && record->process->state() != QProcess::NotRunning) {
        record->process->terminate();
        if (!record->process->waitForFinished(2000)) {
            record->process->kill();
            record->process->waitForFinished(2000);
        }
    }

    removeRecord(instanceId);
}

void ThreadUiProcessManager::drainStandardOutput(ProcessRecord *record) {
    if (record == nullptr || record->process == nullptr) {
        return;
    }

    record->process->readAllStandardOutput();
}

void ThreadUiProcessManager::drainStandardError(ProcessRecord *record) {
    if (record == nullptr || record->process == nullptr) {
        return;
    }

    const QString chunk = QString::fromLocal8Bit(record->process->readAllStandardError());
    if (chunk.isEmpty()) {
        return;
    }

    record->lastStandardError += chunk;
    constexpr int maxStoredCharacters = 4096;
    if (record->lastStandardError.size() > maxStoredCharacters) {
        record->lastStandardError = record->lastStandardError.right(maxStoredCharacters);
    }
}

void ThreadUiProcessManager::terminateAllProcesses() {
    QList<QProcess *> processes;
    processes.reserve(static_cast<qsizetype>(m_processes.size()));

    for (const std::unique_ptr<ProcessRecord> &record : m_processes) {
        if (record == nullptr || record->process == nullptr) {
            continue;
        }

        processes.append(record->process);
    }

    for (QProcess *process : processes) {
        if (process == nullptr || process->state() == QProcess::NotRunning) {
            continue;
        }

        process->terminate();
    }

    for (QProcess *process : processes) {
        if (process == nullptr || process->state() == QProcess::NotRunning) {
            continue;
        }

        if (!process->waitForFinished(2000)) {
            process->kill();
            process->waitForFinished(2000);
        }
    }
}

void ThreadUiProcessManager::onThreadUiAuthenticated(const QString &token) {
    ProcessRecord *record = recordForLaunchToken(token);
    if (record == nullptr) {
        return;
    }

    record->authenticated = true;
    flushPendingAddItems(record);
}

void ThreadUiProcessManager::onThreadUiDisconnected(const QString &token) {
    ProcessRecord *record = recordForLaunchToken(token);
    if (record == nullptr) {
        return;
    }

    record->authenticated = false;
}

}  // namespace qodex::app
