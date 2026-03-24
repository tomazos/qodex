#include "app/ThreadUiProcess.h"

#include <QFileInfo>
#include <QProcess>

#include "threadui/ThreadUiIpcServer.h"

namespace qodex::app {

namespace {

QString appendStderrMessage(const QString &baseMessage, const QString &stderrText) {
    const QString trimmedStderr = stderrText.trimmed();
    if (trimmedStderr.isEmpty()) {
        return baseMessage;
    }

    return QStringLiteral("%1 %2").arg(baseMessage, trimmedStderr);
}

}  // namespace

ThreadUiProcess::ThreadUiProcess(
    const int instanceId,
    const QString &threadId,
    const QString &title,
    const QString &threadUiAppDir,
    const QString &threadUiStartScriptPath,
    const QString &nodeProgram,
    qodex::threadui::ThreadUiIpcServer *threadUiIpcServer,
    QObject *parent
)
    : QObject(parent),
      m_instanceId(instanceId),
      m_threadId(threadId),
      m_title(title),
      m_threadUiAppDir(threadUiAppDir),
      m_threadUiStartScriptPath(threadUiStartScriptPath),
      m_nodeProgram(nodeProgram),
      m_threadUiIpcServer(threadUiIpcServer) {
    Q_ASSERT(m_threadUiIpcServer != nullptr);
}

ThreadUiProcess::~ThreadUiProcess() {
    terminate();
}

int ThreadUiProcess::instanceId() const {
    return m_instanceId;
}

QString ThreadUiProcess::threadId() const {
    return m_threadId;
}

QString ThreadUiProcess::title() const {
    return effectiveTitle();
}

QString ThreadUiProcess::launchToken() const {
    return m_launchToken;
}

bool ThreadUiProcess::isRunning() const {
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

bool ThreadUiProcess::isAuthenticated() const {
    return m_authenticated;
}

bool ThreadUiProcess::matchesLaunchToken(const QString &launchToken) const {
    return !m_launchToken.isEmpty() && m_launchToken == launchToken;
}

ThreadUiProcessInfo ThreadUiProcess::info() const {
    return ThreadUiProcessInfo{
        .instanceId = m_instanceId,
        .title = title(),
        .processId = m_process != nullptr ? m_process->processId() : 0,
    };
}

void ThreadUiProcess::relaunch(const QString &title) {
    m_title = title;
    terminate();
    startProcess();
}

void ThreadUiProcess::terminate() {
    if (m_process == nullptr) {
        m_authenticated = false;
        m_launchToken.clear();
        return;
    }

    QProcess *process = m_process;
    QObject::disconnect(process, nullptr, this, nullptr);

    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(2000)) {
            process->kill();
            process->waitForFinished(2000);
        }
    }

    resetProcess();
    emit activeStateChanged();
}

bool ThreadUiProcess::activate(QString *errorMessage) {
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("That Thread UI subprocess is no longer running.");
        }
        return false;
    }

    const qint64 bytesWritten = m_process->write("activate\n");
    if (bytesWritten <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to activate %1.").arg(title());
        }
        return false;
    }

    return true;
}

void ThreadUiProcess::queueAddItems(const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request) {
    m_pendingAddItemsRequest = request;
    flushPendingAddItems();
}

bool ThreadUiProcess::replyToUserInputRequest(
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const QString &message,
    QString *errorMessage
) {
    if (m_threadUiIpcServer == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Thread UI IPC server is not available.");
        }
        return false;
    }

    return m_threadUiIpcServer->sendUserInputResponse(m_launchToken, requestId, status, message, errorMessage);
}

void ThreadUiProcess::handleAuthenticated() {
    m_authenticated = true;
    flushPendingAddItems();
}

void ThreadUiProcess::handleDisconnected() {
    m_authenticated = false;
}

void ThreadUiProcess::handleUserInputRequested(const std::uint64_t requestId, const QString &text) {
    emit userInputRequested(requestId, text);
}

void ThreadUiProcess::startProcess() {
    if (m_threadUiIpcServer == nullptr) {
        emit statusMessageRequested(QStringLiteral("Unable to launch Thread UI: IPC server is not available."));
        return;
    }

    if (!m_threadUiIpcServer->isListening()) {
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

    const qodex::threadui::ThreadUiLaunchConfig launchConfig = m_threadUiIpcServer->allocateLaunchConfig();
    m_launchToken = launchConfig.token;
    m_authenticated = false;
    m_lastStandardError.clear();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_threadUiAppDir);
    m_process->setProgram(m_nodeProgram);
    m_process->setArguments({
        m_threadUiStartScriptPath,
        QStringLiteral("--qodex-title=%1").arg(title()),
        QStringLiteral("--qodex-ipc-host=%1").arg(launchConfig.host),
        QStringLiteral("--qodex-ipc-port=%1").arg(launchConfig.port),
        QStringLiteral("--qodex-ipc-token=%1").arg(launchConfig.token),
    });

    QObject::connect(m_process, &QProcess::started, this, [this] {
        emit activeStateChanged();
    });
    QObject::connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        drainStandardOutput();
    });
    QObject::connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        drainStandardError();
    });
    QObject::connect(
        m_process,
        &QProcess::errorOccurred,
        this,
        [this](const QProcess::ProcessError error) {
            const QString baseMessage = error == QProcess::FailedToStart
                ? QStringLiteral("Failed to start %1.").arg(title())
                : QStringLiteral("%1 reported a process error.").arg(title());
            emit statusMessageRequested(appendStderrMessage(baseMessage, m_lastStandardError));
            resetProcess();
            emit activeStateChanged();
        }
    );
    QObject::connect(
        m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                emit statusMessageRequested(
                    appendStderrMessage(
                        QStringLiteral("%1 crashed with exit code %2.").arg(title()).arg(exitCode),
                        m_lastStandardError
                    )
                );
            }
            resetProcess();
            emit activeStateChanged();
        }
    );

    m_process->start();
    emit activeStateChanged();
    emit statusMessageRequested(QStringLiteral("Launching %1...").arg(title()));
}

void ThreadUiProcess::resetProcess() {
    if (m_process != nullptr) {
        QObject::disconnect(m_process, nullptr, this, nullptr);
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_authenticated = false;
    m_launchToken.clear();
}

void ThreadUiProcess::flushPendingAddItems() {
    if (!m_authenticated || !m_pendingAddItemsRequest.has_value() || m_threadUiIpcServer == nullptr) {
        return;
    }

    QString errorMessage;
    if (!m_threadUiIpcServer->sendAddItems(m_launchToken, *m_pendingAddItemsRequest, &errorMessage)) {
        emit statusMessageRequested(
            QStringLiteral("Failed to deliver thread history to %1: %2").arg(title(), errorMessage)
        );
        return;
    }

    m_pendingAddItemsRequest.reset();
}

void ThreadUiProcess::drainStandardOutput() {
    if (m_process == nullptr) {
        return;
    }

    m_process->readAllStandardOutput();
}

void ThreadUiProcess::drainStandardError() {
    if (m_process == nullptr) {
        return;
    }

    const QString chunk = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (chunk.isEmpty()) {
        return;
    }

    m_lastStandardError += chunk;
    constexpr int maxStoredCharacters = 4096;
    if (m_lastStandardError.size() > maxStoredCharacters) {
        m_lastStandardError = m_lastStandardError.right(maxStoredCharacters);
    }
}

QString ThreadUiProcess::effectiveTitle() const {
    const QString trimmedTitle = m_title.trimmed();
    return trimmedTitle.isEmpty() ? m_threadId : trimmedTitle;
}

}  // namespace qodex::app
