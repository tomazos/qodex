#include "app/SessionController.h"

#include <QDateTime>
#include <QLocale>
#include <QProcess>

#include "CodexClient.h"
#include "CodexProtocol.h"
#include "app/AppConfig.h"
#include "codex/AppServerTransport.h"
#include "domain/ThreadStore.h"
#include "ui/MainWindow.h"
#include "ui/ThreadListPane.h"

namespace qodex::app {

using qodex::codex::ClientInfo;
using qodex::codex::CodexClient;
using qodex::codex::InitializeResponse;
using qodex::codex::JsonRpcErrorObject;
using qodex::codex::JsonRpcId;
using qodex::codex::Nullable;
using qodex::codex::Ref;
using qodex::codex::Thread;
using qodex::codex::ThreadArchivedNotificationParams;
using qodex::codex::ThreadListResponse;
using qodex::codex::ThreadNameUpdatedNotificationParams;
using qodex::codex::ThreadSortKey;
using qodex::codex::ThreadSourceKind;
using qodex::codex::ThreadStartedNotificationParams;
using qodex::codex::ThreadStatus;
using qodex::codex::ThreadStatusChangedNotificationParams;
using qodex::codex::ThreadUnarchivedNotificationParams;

SessionController::SessionController(
    const AppConfig &config,
    qodex::codex::AppServerTransport *transport,
    CodexClient *client,
    qodex::domain::ThreadStore *threadStore,
    qodex::ui::MainWindow *mainWindow,
    QObject *parent
)
    : QObject(parent),
      m_config(config),
      m_transport(transport),
      m_client(client),
      m_threadStore(threadStore),
      m_mainWindow(mainWindow) {
    Q_ASSERT(m_transport != nullptr);
    Q_ASSERT(m_client != nullptr);
    Q_ASSERT(m_threadStore != nullptr);
    Q_ASSERT(m_mainWindow != nullptr);

    connect(m_transport, &qodex::codex::AppServerTransport::started, this, &SessionController::onTransportStarted);

    connect(m_client, &CodexClient::initializeSucceeded, this, &SessionController::onInitializeSucceeded);
    connect(m_client, &CodexClient::initializeFailed, this, &SessionController::onInitializeFailed);
    connect(m_client, &CodexClient::threadListSucceeded, this, &SessionController::onThreadListSucceeded);
    connect(m_client, &CodexClient::threadListFailed, this, &SessionController::onThreadListFailed);
    connect(
        m_client,
        &CodexClient::threadStartedNotificationReceived,
        this,
        &SessionController::onThreadStartedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadNameUpdatedNotificationReceived,
        this,
        &SessionController::onThreadNameUpdatedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadStatusChangedNotificationReceived,
        this,
        &SessionController::onThreadStatusChangedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadArchivedNotificationReceived,
        this,
        &SessionController::onThreadArchivedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::threadUnarchivedNotificationReceived,
        this,
        &SessionController::onThreadUnarchivedNotificationReceived
    );
    connect(
        m_client,
        &CodexClient::transportErrorOccurred,
        this,
        &SessionController::onTransportErrorOccurred
    );
    connect(
        m_client,
        &CodexClient::transportProcessExited,
        this,
        &SessionController::onTransportProcessExited
    );

    connect(
        m_mainWindow->threadListPane(),
        &qodex::ui::ThreadListPane::refreshRequested,
        this,
        &SessionController::onRefreshRequested
    );
    connect(
        m_mainWindow->threadListPane(),
        &qodex::ui::ThreadListPane::threadSelected,
        this,
        &SessionController::onThreadSelected
    );
    connect(
        m_threadStore,
        &qodex::domain::ThreadStore::threadListChanged,
        this,
        &SessionController::refreshSelectedThreadUi
    );
    connect(
        m_threadStore,
        &qodex::domain::ThreadStore::selectedThreadChanged,
        this,
        &SessionController::refreshSelectedThreadUi
    );
}

void SessionController::start() {
    if (m_startRequested) {
        return;
    }
    m_startRequested = true;

    if (m_config.codexProgram.isEmpty()) {
        m_mainWindow->setStatusMessage(QStringLiteral("codex executable not found on PATH."));
        m_mainWindow->setThreadSummaryText(
            QStringLiteral("Install the Codex CLI or add it to PATH, then restart qodex.")
        );
        return;
    }

    m_mainWindow->setStatusMessage(QStringLiteral("Starting codex app-server..."));
    m_mainWindow->setThreadSummaryText(
        QStringLiteral("Connecting to codex app-server and loading threads...")
    );
    m_transport->start(m_config.codexProgram, m_config.codexArguments);
}

void SessionController::onTransportStarted() {
    auto clientInfo = Ref<ClientInfo>::create();
    clientInfo->name = m_config.clientName;
    clientInfo->version = m_config.clientVersion;

    m_mainWindow->setStatusMessage(QStringLiteral("Connected. Initializing Codex session..."));
    const JsonRpcId requestId = m_client->sendInitializeRequest(
        Nullable<Ref<qodex::codex::InitializeCapabilities>>::missing(),
        clientInfo
    );
    if (!requestId.isValid()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send initialize request."));
    }
}

void SessionController::onInitializeSucceeded(const JsonRpcId &id, const InitializeResponse &response) {
    Q_UNUSED(id);
    Q_UNUSED(response);

    if (!m_client->sendInitializedNotification()) {
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send initialized notification."));
        return;
    }

    requestThreadList();
}

void SessionController::onInitializeFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    Q_UNUSED(id);
    m_mainWindow->setStatusMessage(
        QStringLiteral("Initialize failed: %1").arg(error.message)
    );
}

void SessionController::onThreadListSucceeded(const JsonRpcId &id, const ThreadListResponse &response) {
    Q_UNUSED(id);

    QList<qodex::domain::ThreadSummary> summaries;
    summaries.reserve(response.data.size());
    for (const Ref<Thread> &thread : response.data) {
        if (!thread) {
            continue;
        }
        summaries.append(projectThreadSummary(*thread));
    }

    m_threadListRequestInFlight = false;
    m_threadStore->replaceThreadSummaries(std::move(summaries));
    m_mainWindow->setStatusMessage(
        QStringLiteral("Loaded %1 threads.").arg(m_threadStore->threadSummaries().size())
    );
}

void SessionController::onThreadListFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    Q_UNUSED(id);
    m_threadListRequestInFlight = false;
    m_mainWindow->setStatusMessage(
        QStringLiteral("thread/list failed: %1").arg(error.message)
    );
}

void SessionController::onRefreshRequested() {
    requestThreadList();
}

void SessionController::onThreadSelected(const QString &threadId) {
    m_threadStore->setSelectedThreadId(threadId);
}

void SessionController::onTransportErrorOccurred(const QString &message) {
    m_mainWindow->setStatusMessage(message);
}

void SessionController::onTransportProcessExited(const int exitCode, const QProcess::ExitStatus exitStatus) {
    const QString statusText = exitStatus == QProcess::NormalExit
        ? QStringLiteral("exited")
        : QStringLiteral("crashed");
    m_mainWindow->setStatusMessage(
        QStringLiteral("codex app-server %1 (code %2).").arg(statusText).arg(exitCode)
    );
}

void SessionController::onThreadStartedNotificationReceived(const ThreadStartedNotificationParams &params) {
    if (!params.thread) {
        return;
    }
    m_threadStore->upsertThreadSummary(projectThreadSummary(*params.thread));
}

void SessionController::onThreadNameUpdatedNotificationReceived(const ThreadNameUpdatedNotificationParams &params) {
    const auto existing = m_threadStore->threadSummaryById(params.threadId);
    if (!existing.has_value()) {
        return;
    }

    if (params.threadName.isMissing()) {
        return;
    }

    const QString title = params.threadName.hasValue() && !params.threadName.value().trimmed().isEmpty()
        ? params.threadName.value().trimmed()
        : (!existing->preview.trimmed().isEmpty() ? existing->preview.trimmed() : existing->id);
    m_threadStore->updateThreadTitle(params.threadId, title);
}

void SessionController::onThreadStatusChangedNotificationReceived(const ThreadStatusChangedNotificationParams &params) {
    if (!params.status) {
        return;
    }
    m_threadStore->updateThreadStatusText(params.threadId, threadStatusText(*params.status));
}

void SessionController::onThreadArchivedNotificationReceived(const ThreadArchivedNotificationParams &params) {
    m_threadStore->removeThreadSummary(params.threadId);
}

void SessionController::onThreadUnarchivedNotificationReceived(const ThreadUnarchivedNotificationParams &params) {
    Q_UNUSED(params);
    requestThreadList();
}

void SessionController::refreshSelectedThreadUi() {
    m_mainWindow->threadListPane()->setCurrentThreadId(m_threadStore->selectedThreadId());

    const auto summary = m_threadStore->threadSummaryById(m_threadStore->selectedThreadId());
    if (!summary.has_value()) {
        m_mainWindow->setThreadSummaryText(
            QStringLiteral("Select a thread from the list to inspect its summary.")
        );
        return;
    }

    m_mainWindow->setThreadSummaryText(formatThreadSummaryText(*summary));
}

qodex::domain::ThreadSummary SessionController::projectThreadSummary(const Thread &thread) const {
    return qodex::domain::ThreadSummary{
        .id = thread.id,
        .title = threadDisplayTitle(thread),
        .preview = thread.preview.trimmed(),
        .cwd = thread.cwd,
        .statusText = thread.status ? threadStatusText(*thread.status) : QStringLiteral("Unknown"),
        .updatedAt = thread.updatedAt,
    };
}

QString SessionController::threadStatusText(const ThreadStatus &status) const {
    switch (status.kind) {
    case ThreadStatus::Kind::NotLoaded:
        return QStringLiteral("Not Loaded");
    case ThreadStatus::Kind::Idle:
        return QStringLiteral("Idle");
    case ThreadStatus::Kind::SystemError:
        return QStringLiteral("System Error");
    case ThreadStatus::Kind::Active:
        return QStringLiteral("Active");
    }

    return QStringLiteral("Unknown");
}

QString SessionController::threadDisplayTitle(const Thread &thread) const {
    if (thread.name.hasValue() && !thread.name.value().trimmed().isEmpty()) {
        return thread.name.value().trimmed();
    }
    if (!thread.preview.trimmed().isEmpty()) {
        return thread.preview.trimmed();
    }
    return thread.id;
}

QString SessionController::formatThreadSummaryText(const qodex::domain::ThreadSummary &summary) const {
    const QString updatedText = QLocale().toString(QDateTime::fromSecsSinceEpoch(summary.updatedAt), QLocale::ShortFormat);
    return QStringLiteral(
        "<b>%1</b><br><br>"
        "<b>Status:</b> %2<br>"
        "<b>ID:</b> %3<br>"
        "<b>CWD:</b> %4<br>"
        "<b>Updated:</b> %5<br><br>"
        "<b>Preview:</b><br>%6"
    ).arg(
        summary.title.toHtmlEscaped(),
        summary.statusText.toHtmlEscaped(),
        summary.id.toHtmlEscaped(),
        summary.cwd.toHtmlEscaped(),
        updatedText.toHtmlEscaped(),
        summary.preview.isEmpty() ? QStringLiteral("(empty)") : summary.preview.toHtmlEscaped()
    );
}

void SessionController::requestThreadList() {
    if (m_threadListRequestInFlight) {
        return;
    }

    m_threadListRequestInFlight = true;
    m_mainWindow->setStatusMessage(QStringLiteral("Loading threads..."));

    const JsonRpcId requestId = m_client->sendThreadListRequest(
        missing<bool>(),
        missing<QString>(),
        missing<QString>(),
        missing<qint64>(),
        missing<QList<QString>>(),
        missing<QString>(),
        missing<ThreadSortKey>(),
        missing<QList<ThreadSourceKind>>()
    );
    if (!requestId.isValid()) {
        m_threadListRequestInFlight = false;
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/list request."));
    }
}

}  // namespace qodex::app
