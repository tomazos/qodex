#include "app/SessionController.h"

#include <algorithm>

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
using qodex::codex::EmptyObject;
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
using qodex::codex::ThreadUnarchiveResponse;
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
    connect(m_client, &CodexClient::threadArchiveSucceeded, this, &SessionController::onThreadArchiveSucceeded);
    connect(m_client, &CodexClient::threadArchiveFailed, this, &SessionController::onThreadArchiveFailed);
    connect(m_client, &CodexClient::threadUnarchiveSucceeded, this, &SessionController::onThreadUnarchiveSucceeded);
    connect(m_client, &CodexClient::threadUnarchiveFailed, this, &SessionController::onThreadUnarchiveFailed);
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
        m_mainWindow->threadListPane(),
        &qodex::ui::ThreadListPane::archiveThreadsRequested,
        this,
        &SessionController::onArchiveThreadsRequested
    );
    connect(
        m_mainWindow->threadListPane(),
        &qodex::ui::ThreadListPane::unarchiveThreadsRequested,
        this,
        &SessionController::onUnarchiveThreadsRequested
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
        return;
    }

    m_mainWindow->setStatusMessage(QStringLiteral("Starting codex app-server..."));
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

    requestThreadLists();
}

void SessionController::onInitializeFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    Q_UNUSED(id);
    m_mainWindow->setStatusMessage(
        QStringLiteral("Initialize failed: %1").arg(error.message)
    );
}

void SessionController::onThreadListSucceeded(const JsonRpcId &id, const ThreadListResponse &response) {
    const QString requestKey = id.toKey();
    const bool isArchivedRequest = requestKey == m_archivedThreadListRequestKey;
    const bool isActiveRequest = requestKey == m_activeThreadListRequestKey;
    if (!isArchivedRequest && !isActiveRequest) {
        return;
    }

    QList<qodex::domain::ThreadSummary> summaries;
    summaries.reserve(response.data.size());
    for (const Ref<Thread> &thread : response.data) {
        if (!thread) {
            continue;
        }
        summaries.append(projectThreadSummary(*thread, isArchivedRequest));
    }

    if (isArchivedRequest) {
        m_archivedThreadListRequestInFlight = false;
        m_archivedThreadListRequestKey.clear();
    } else {
        m_activeThreadListRequestInFlight = false;
        m_activeThreadListRequestKey.clear();
    }

    m_threadStore->replaceThreadSummaries(std::move(summaries), isArchivedRequest);

    if (!m_activeThreadListRequestInFlight && !m_archivedThreadListRequestInFlight) {
        const QList<qodex::domain::ThreadSummary> allThreads = m_threadStore->threadSummaries();
        const qsizetype archivedCount = static_cast<qsizetype>(std::count_if(
            allThreads.begin(),
            allThreads.end(),
            [](const qodex::domain::ThreadSummary &summary) { return summary.archived; }
        ));
        const qsizetype activeCount = allThreads.size() - archivedCount;
        m_mainWindow->setStatusMessage(
            QStringLiteral("Loaded %1 threads (%2 active, %3 archived).")
                .arg(allThreads.size())
                .arg(activeCount)
                .arg(archivedCount)
        );
    }
}

void SessionController::onThreadListFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString requestKey = id.toKey();
    if (requestKey == m_archivedThreadListRequestKey) {
        m_archivedThreadListRequestInFlight = false;
        m_archivedThreadListRequestKey.clear();
    } else if (requestKey == m_activeThreadListRequestKey) {
        m_activeThreadListRequestInFlight = false;
        m_activeThreadListRequestKey.clear();
    } else {
        return;
    }

    m_mainWindow->setStatusMessage(
        QStringLiteral("thread/list failed: %1").arg(error.message)
    );
}

void SessionController::onRefreshRequested() {
    requestThreadLists();
}

void SessionController::onThreadSelected(const QString &threadId) {
    m_threadStore->setSelectedThreadId(threadId);
}

void SessionController::onArchiveThreadsRequested(const QStringList &threadIds) {
    QStringList queuedThreadIds;
    queuedThreadIds.reserve(threadIds.size());

    for (const QString &threadId : threadIds) {
        const auto summary = m_threadStore->threadSummaryById(threadId);
        if (!summary.has_value() || summary->archived) {
            continue;
        }

        const JsonRpcId requestId = m_client->sendThreadArchiveRequest(threadId);
        if (!requestId.isValid()) {
            m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/archive request."));
            continue;
        }

        m_pendingArchiveRequests.insert(requestId.toKey(), threadId);
        queuedThreadIds.append(threadId);
    }

    if (!queuedThreadIds.isEmpty()) {
        m_mainWindow->setStatusMessage(
            queuedThreadIds.size() == 1 ? QStringLiteral("Archiving thread...")
                                        : QStringLiteral("Archiving %1 threads...").arg(queuedThreadIds.size())
        );
    }
}

void SessionController::onUnarchiveThreadsRequested(const QStringList &threadIds) {
    QStringList queuedThreadIds;
    queuedThreadIds.reserve(threadIds.size());

    for (const QString &threadId : threadIds) {
        const auto summary = m_threadStore->threadSummaryById(threadId);
        if (!summary.has_value() || !summary->archived) {
            continue;
        }

        const JsonRpcId requestId = m_client->sendThreadUnarchiveRequest(threadId);
        if (!requestId.isValid()) {
            m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/unarchive request."));
            continue;
        }

        m_pendingUnarchiveRequests.insert(requestId.toKey(), threadId);
        queuedThreadIds.append(threadId);
    }

    if (!queuedThreadIds.isEmpty()) {
        m_mainWindow->setStatusMessage(
            queuedThreadIds.size() == 1 ? QStringLiteral("Unarchiving thread...")
                                        : QStringLiteral("Unarchiving %1 threads...").arg(queuedThreadIds.size())
        );
    }
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

void SessionController::onThreadArchiveSucceeded(const JsonRpcId &id, EmptyObject response) {
    Q_UNUSED(response);

    const QString requestKey = id.toKey();
    const QString threadId = m_pendingArchiveRequests.take(requestKey);
    if (threadId.isEmpty()) {
        return;
    }

    if (!m_threadStore->setThreadArchived(threadId, true)) {
        requestThreadList(true);
    }
}

void SessionController::onThreadArchiveFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString threadId = m_pendingArchiveRequests.take(id.toKey());
    if (!threadId.isEmpty()) {
        m_mainWindow->setStatusMessage(
            QStringLiteral("Failed to archive thread %1: %2").arg(threadId, error.message)
        );
    }
}

void SessionController::onThreadUnarchiveSucceeded(const JsonRpcId &id, const ThreadUnarchiveResponse &response) {
    const QString requestKey = id.toKey();
    const QString requestedThreadId = m_pendingUnarchiveRequests.take(requestKey);
    if (!response.thread) {
        if (!requestedThreadId.isEmpty()) {
            requestThreadList(false);
        }
        return;
    }

    m_threadStore->upsertThreadSummary(projectThreadSummary(*response.thread, false));
}

void SessionController::onThreadUnarchiveFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const QString threadId = m_pendingUnarchiveRequests.take(id.toKey());
    if (!threadId.isEmpty()) {
        m_mainWindow->setStatusMessage(
            QStringLiteral("Failed to unarchive thread %1: %2").arg(threadId, error.message)
        );
    }
}

void SessionController::onThreadStartedNotificationReceived(const ThreadStartedNotificationParams &params) {
    if (!params.thread) {
        return;
    }
    m_threadStore->upsertThreadSummary(projectThreadSummary(*params.thread, false));
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
    if (!m_threadStore->setThreadArchived(params.threadId, true)) {
        requestThreadList(true);
    }
}

void SessionController::onThreadUnarchivedNotificationReceived(const ThreadUnarchivedNotificationParams &params) {
    if (!m_threadStore->setThreadArchived(params.threadId, false)) {
        requestThreadList(false);
    }
}

void SessionController::refreshSelectedThreadUi() {
    m_mainWindow->threadListPane()->setCurrentThreadId(m_threadStore->selectedThreadId());
}

qodex::domain::ThreadSummary SessionController::projectThreadSummary(const Thread &thread, const bool archived) const {
    return qodex::domain::ThreadSummary{
        .id = thread.id,
        .title = threadDisplayTitle(thread),
        .preview = thread.preview.trimmed(),
        .cwd = thread.cwd,
        .statusText = thread.status ? threadStatusText(*thread.status) : QStringLiteral("Unknown"),
        .archived = archived,
        .createdAt = thread.createdAt,
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

void SessionController::requestThreadLists() {
    requestThreadList(false);
    requestThreadList(true);
}

void SessionController::requestThreadList(const bool archived) {
    const bool inFlight = archived ? m_archivedThreadListRequestInFlight : m_activeThreadListRequestInFlight;
    if (inFlight) {
        return;
    }

    if (archived) {
        m_archivedThreadListRequestInFlight = true;
    } else {
        m_activeThreadListRequestInFlight = true;
    }
    m_mainWindow->setStatusMessage(QStringLiteral("Loading threads..."));

    const JsonRpcId requestId = m_client->sendThreadListRequest(
        Nullable<bool>::fromValue(archived),
        missing<QString>(),
        missing<QString>(),
        missing<qint64>(),
        missing<QList<QString>>(),
        missing<QString>(),
        missing<ThreadSortKey>(),
        missing<QList<ThreadSourceKind>>()
    );
    if (!requestId.isValid()) {
        if (archived) {
            m_archivedThreadListRequestInFlight = false;
        } else {
            m_activeThreadListRequestInFlight = false;
        }
        m_mainWindow->setStatusMessage(QStringLiteral("Failed to send thread/list request."));
        return;
    }

    if (archived) {
        m_archivedThreadListRequestKey = requestId.toKey();
    } else {
        m_activeThreadListRequestKey = requestId.toKey();
    }
}

}  // namespace qodex::app
