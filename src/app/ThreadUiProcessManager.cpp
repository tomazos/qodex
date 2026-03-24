#include "app/ThreadUiProcessManager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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

ThreadUiProcessManager::ThreadUiProcessManager(QObject *parent)
    : QObject(parent),
      m_threadUiAppDir(resolveThreadUiAppDir()),
      m_threadUiStartScriptPath(resolveThreadUiStartScriptPath(m_threadUiAppDir)),
      m_nodeProgram(resolveNodeProgram()) {}

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

void ThreadUiProcessManager::launchThreadUi() {
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
    const QString title = QStringLiteral("Thread UI %1").arg(instanceId);

    auto record = std::make_unique<ProcessRecord>();
    record->instanceId = instanceId;
    record->title = title;
    record->process = new QProcess(this);
    record->process->setWorkingDirectory(m_threadUiAppDir);
    record->process->setProgram(m_nodeProgram);
    record->process->setArguments({
        m_threadUiStartScriptPath,
        QStringLiteral("--qodex-title=%1").arg(title),
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
    emit statusMessageRequested(QStringLiteral("Launching %1...").arg(title));
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
#ifdef QODEX_THREAD_UI_APP_DIR
    const QString configuredPath = QString::fromLocal8Bit(QODEX_THREAD_UI_APP_DIR);
    if (!configuredPath.isEmpty()) {
        return QDir::cleanPath(configuredPath);
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

}  // namespace qodex::app
