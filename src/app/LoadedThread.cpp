#include "app/LoadedThread.h"

#include "app/ThreadUiProcess.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedItem.h"
#include "domain/threadmodel/InprogressItem.h"

namespace qodex::app {

namespace {

constexpr qint64 kJsonRpcInvalidRequestErrorCode = -32600;

bool isNoActiveTurnToSteerError(const qodex::codex::JsonRpcErrorObject &error) {
    return error.code == kJsonRpcInvalidRequestErrorCode &&
        error.message.trimmed().compare(QStringLiteral("no active turn to steer"), Qt::CaseInsensitive) == 0;
}

QString threadStatusKindString(const qodex::codex::ThreadStatus &status) {
    switch (status.kind) {
    case qodex::codex::ThreadStatus::Kind::NotLoaded:
        return QStringLiteral("not_loaded");
    case qodex::codex::ThreadStatus::Kind::Idle:
        return QStringLiteral("idle");
    case qodex::codex::ThreadStatus::Kind::SystemError:
        return QStringLiteral("system_error");
    case qodex::codex::ThreadStatus::Kind::Active:
        return QStringLiteral("active");
    }

    return QStringLiteral("unknown");
}

QString activeFlagText(const qodex::codex::ThreadActiveFlag activeFlag) {
    switch (activeFlag) {
    case qodex::codex::ThreadActiveFlag::WaitingOnApproval:
        return QStringLiteral("Waiting on approval");
    case qodex::codex::ThreadActiveFlag::WaitingOnUserInput:
        return QStringLiteral("Waiting on user input");
    }

    return QStringLiteral("Unknown");
}

QString activeFlagIdentifier(const qodex::codex::ThreadActiveFlag activeFlag) {
    switch (activeFlag) {
    case qodex::codex::ThreadActiveFlag::WaitingOnApproval:
        return QStringLiteral("waiting_on_approval");
    case qodex::codex::ThreadActiveFlag::WaitingOnUserInput:
        return QStringLiteral("waiting_on_user_input");
    }

    return QStringLiteral("unknown");
}

QString threadStatusText(
    const qodex::codex::ThreadStatus &status,
    QStringList *activeFlags = nullptr
) {
    if (activeFlags != nullptr) {
        activeFlags->clear();
    }

    switch (status.kind) {
    case qodex::codex::ThreadStatus::Kind::NotLoaded:
        return QStringLiteral("Not Loaded");
    case qodex::codex::ThreadStatus::Kind::Idle:
        return QStringLiteral("Idle");
    case qodex::codex::ThreadStatus::Kind::SystemError:
        return QStringLiteral("System Error");
    case qodex::codex::ThreadStatus::Kind::Active:
    {
        QStringList activeFlagTexts;
        QStringList activeFlagIds;
        if (const auto *payload = std::get_if<qodex::codex::Ref<qodex::codex::ThreadStatusActive>>(&status.payload);
            payload != nullptr && *payload) {
            for (const qodex::codex::ThreadActiveFlag activeFlag : (*payload)->activeFlags) {
                activeFlagTexts.append(activeFlagText(activeFlag));
                activeFlagIds.append(activeFlagIdentifier(activeFlag));
            }
        }

        if (activeFlags != nullptr) {
            *activeFlags = activeFlagIds;
        }
        return activeFlagTexts.isEmpty()
            ? QStringLiteral("Active")
            : QStringLiteral("Active - %1").arg(activeFlagTexts.join(QStringLiteral(", ")));
    }
    }

    return QStringLiteral("Unknown");
}

QString threadItemId(const qodex::codex::ThreadItem &item) {
    switch (item.kind) {
    case qodex::codex::ThreadItem::Kind::UserMessage:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemUserMessage>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::AgentMessage:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemAgentMessage>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::Plan:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemPlan>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::Reasoning:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemReasoning>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::CommandExecution:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemCommandExecution>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::FileChange:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemFileChange>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::McpToolCall:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemMcpToolCall>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::DynamicToolCall:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemDynamicToolCall>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::CollabAgentToolCall:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemCollabAgentToolCall>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::WebSearch:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemWebSearch>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::ImageView:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemImageView>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::ImageGeneration:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemImageGeneration>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::EnteredReviewMode:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemEnteredReviewMode>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::ExitedReviewMode:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemExitedReviewMode>>(item.payload)->id;
    case qodex::codex::ThreadItem::Kind::ContextCompaction:
        return std::get<qodex::codex::Ref<qodex::codex::ThreadItemContextCompaction>>(item.payload)->id;
    }

    return {};
}

}  // namespace

using qodex::codex::JsonRpcErrorObject;
using qodex::codex::JsonRpcId;
using qodex::codex::Nullable;
using qodex::codex::Ref;
using qodex::codex::Thread;
using qodex::codex::ThreadResumeResponse;
using qodex::codex::ThreadStatus;
using qodex::codex::ThreadItem;
using qodex::codex::Turn;
using qodex::codex::TurnCompletedNotificationParams;
using qodex::codex::TurnStartResponse;
using qodex::codex::TurnStartedNotificationParams;
using qodex::codex::TurnStatus;
using qodex::codex::TurnSteerResponse;
using qodex::codex::UserInput;
using qodex::codex::UserInputText;
using qodex::domain::threadmodel::AbstractItem;

LoadedThread::LoadedThread(
    const QString &threadId,
    const QString &initialTitle,
    qodex::codex::CodexClient *client,
    ThreadUiProcess *threadUiProcess,
    QObject *parent
)
    : QObject(parent),
      m_threadId(threadId),
      m_title(initialTitle.trimmed().isEmpty() ? threadId : initialTitle.trimmed()),
      m_client(client),
      m_threadUiProcess(threadUiProcess) {
    Q_ASSERT(!m_threadId.isEmpty());
    Q_ASSERT(m_client != nullptr);
    Q_ASSERT(m_threadUiProcess != nullptr);

    QObject::connect(m_threadUiProcess, &ThreadUiProcess::userInputRequested, this, &LoadedThread::onThreadUiUserInputRequested);
    QObject::connect(m_threadUiProcess, &ThreadUiProcess::resolveLinkRequested, this, &LoadedThread::onThreadUiResolveLinkRequested);
    QObject::connect(m_client, &qodex::codex::CodexClient::turnStartSucceeded, this, &LoadedThread::onTurnStartSucceeded);
    QObject::connect(m_client, &qodex::codex::CodexClient::turnStartFailed, this, &LoadedThread::onTurnStartFailed);
    QObject::connect(m_client, &qodex::codex::CodexClient::turnSteerSucceeded, this, &LoadedThread::onTurnSteerSucceeded);
    QObject::connect(m_client, &qodex::codex::CodexClient::turnSteerFailed, this, &LoadedThread::onTurnSteerFailed);
}

QString LoadedThread::threadId() const {
    return m_threadId;
}

const QString &LoadedThread::title() const {
    return m_title;
}

const QString &LoadedThread::activeTurnId() const {
    return m_activeTurnId;
}

QList<const qodex::domain::threadmodel::Turn *> LoadedThread::orderedTurns() const {
    QList<const qodex::domain::threadmodel::Turn *> turns;
    turns.reserve(m_turnOrder.size());

    for (const QString &turnId : m_turnOrder) {
        if (const auto *turn = turnForId(turnId)) {
            turns.append(turn);
        }
    }

    return turns;
}

void LoadedThread::resume(const QString &title, const ThreadResumeResponse &response) {
    m_title = title.trimmed().isEmpty() ? m_threadId : title.trimmed();
    m_cwd = response.thread ? response.thread->cwd : QString{};
    m_activeTurnId = response.thread ? activeTurnIdForThread(*response.thread) : QString{};
    if (response.thread && response.thread->status) {
        applyProtocolThreadStatus(*response.thread->status);
    } else if (!m_activeTurnId.isEmpty()) {
        setDerivedThreadStatus(QStringLiteral("active"), QStringLiteral("Active"));
    } else {
        setDerivedThreadStatus(QStringLiteral("idle"), QStringLiteral("Idle"));
    }
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
    if (response.thread) {
        rebuildModelFromThread(*response.thread);
    }
    m_threadUiProcess->relaunch(m_title);
    queueThreadStatus();
    m_threadUiProcess->queueAddItems(m_threadUiProjector.projectTurns(orderedTurns()));
    emit snapshotRebuilt();
}

void LoadedThread::onThreadClosed() {
    m_cwd.clear();
    m_activeTurnId.clear();
    setDerivedThreadStatus(QStringLiteral("not_loaded"), QStringLiteral("Not Loaded"));
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
    queueThreadStatus();
    emit snapshotRebuilt();
}

void LoadedThread::onThreadStatusChanged(const ThreadStatus &status) {
    const QString previousActiveTurnId = m_activeTurnId;
    applyProtocolThreadStatus(status);
    if (status.kind != ThreadStatus::Kind::Active) {
        m_activeTurnId.clear();
    }
    if (m_activeTurnId != previousActiveTurnId) {
        emit threadPresentationChanged();
    }
    queueThreadStatus();
}

void LoadedThread::onTurnStartedNotification(const TurnStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.turn || params.turn->id.isEmpty()) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turn->id) == nullptr;
    ensureTurn(params.turn->id)->applyMetadata(*params.turn);
    if (turnWasMissing) {
        emit turnInserted(params.turn->id, turnRow(params.turn->id));
    } else {
        emit turnChanged(params.turn->id);
    }

    const QString previousActiveTurnId = m_activeTurnId;
    m_activeTurnId = params.turn->id;
    setDerivedThreadStatus(
        QStringLiteral("active"),
        m_threadStatusKind == QStringLiteral("active") ? m_threadStatusText : QStringLiteral("Active"),
        m_threadActiveFlags
    );
    if (m_activeTurnId != previousActiveTurnId) {
        emit threadPresentationChanged();
    }
    queueThreadStatus();
}

void LoadedThread::onTurnCompletedNotification(const TurnCompletedNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    if (params.turn && !params.turn->id.isEmpty()) {
        const bool turnWasMissing = turnForId(params.turn->id) == nullptr;
        ensureTurn(params.turn->id)->applyMetadata(*params.turn);
        if (turnWasMissing) {
            emit turnInserted(params.turn->id, turnRow(params.turn->id));
        } else {
            emit turnChanged(params.turn->id);
        }

        const QString previousActiveTurnId = m_activeTurnId;
        if (m_activeTurnId.isEmpty() || m_activeTurnId == params.turn->id) {
            m_activeTurnId.clear();
        }
        setDerivedThreadStatus(QStringLiteral("idle"), QStringLiteral("Idle"));
        if (m_activeTurnId != previousActiveTurnId) {
            emit threadPresentationChanged();
        }
        queueThreadStatus();
        return;
    }

    const QString previousActiveTurnId = m_activeTurnId;
    m_activeTurnId.clear();
    setDerivedThreadStatus(QStringLiteral("idle"), QStringLiteral("Idle"));
    if (m_activeTurnId != previousActiveTurnId) {
        emit threadPresentationChanged();
    }
    queueThreadStatus();
}

void LoadedThread::onItemStartedNotification(const qodex::codex::ItemStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const QString itemId = threadItemId(*params.item);
    const bool itemWasMissing = turn->itemById(itemId) == nullptr;
    if (const auto *startedItem = turn->applyStartedItem(*params.item)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, startedItem->id(), turn->itemRow(startedItem->id()));
        } else {
            emit itemChanged(params.turnId, startedItem->id());
        }
    }
}

void LoadedThread::onItemCompletedNotification(const qodex::codex::ItemCompletedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const QString itemId = threadItemId(*params.item);
    const bool itemWasMissing = turn->itemById(itemId) == nullptr;
    if (auto *item = turn->applyCompletedItem(*params.item)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, item->id(), turn->itemRow(item->id()));
        } else {
            emit itemChanged(params.turnId, item->id());
        }
        queueDisplayItemIfSupported(*item);
    }
}

void LoadedThread::onItemAgentMessageDeltaNotification(
    const qodex::codex::ItemAgentMessageDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyAgentMessageDelta(params.itemId, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemCommandExecutionOutputDeltaNotification(
    const qodex::codex::ItemCommandExecutionOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyCommandExecutionOutputDelta(params.itemId, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemCommandExecutionTerminalInteractionNotification(
    const qodex::codex::ItemCommandExecutionTerminalInteractionNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyCommandExecutionTerminalInteraction(params.itemId, params.processId, params.stdin)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemFileChangeOutputDeltaNotification(
    const qodex::codex::ItemFileChangeOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyFileChangeOutputDelta(params.itemId, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemMcpToolCallProgressNotification(
    const qodex::codex::ItemMcpToolCallProgressNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyMcpToolCallProgress(params.itemId, params.message)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemPlanDeltaNotification(const qodex::codex::ItemPlanDeltaNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyPlanDelta(params.itemId, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemReasoningSummaryPartAddedNotification(
    const qodex::codex::ItemReasoningSummaryPartAddedNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyReasoningSummaryPartAdded(params.itemId, params.summaryIndex)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemReasoningSummaryTextDeltaNotification(
    const qodex::codex::ItemReasoningSummaryTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyReasoningSummaryTextDelta(params.itemId, params.summaryIndex, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onItemReasoningTextDeltaNotification(
    const qodex::codex::ItemReasoningTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    const bool turnWasMissing = turnForId(params.turnId) == nullptr;
    auto *turn = ensureTurn(params.turnId);
    if (turn == nullptr) {
        return;
    }
    if (turnWasMissing) {
        emit turnInserted(params.turnId, turnRow(params.turnId));
    }

    const bool itemWasMissing = turn->itemById(params.itemId) == nullptr;
    if (turn->applyReasoningTextDelta(params.itemId, params.contentIndex, params.delta)) {
        if (itemWasMissing) {
            emit itemInserted(params.turnId, params.itemId, turn->itemRow(params.itemId));
        } else {
            emit itemChanged(params.turnId, params.itemId);
        }
    }
}

void LoadedThread::onThreadUiUserInputRequested(const std::uint64_t requestId, const QString &text) {
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        replyToThreadUiUserInputRequest(
            PendingThreadUiUserInputRequest{
                .requestId = requestId,
                .text = trimmedText,
            },
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            QStringLiteral("Input must not be empty.")
        );
        return;
    }

    PendingThreadUiUserInputRequest pendingRequest{
        .requestId = requestId,
        .text = trimmedText,
        .dispatchKind = m_activeTurnId.isEmpty()
            ? PendingThreadUiUserInputDispatchKind::TurnStart
            : PendingThreadUiUserInputDispatchKind::TurnSteer,
    };

    QString errorMessage;
    if (!requeuePendingThreadUiUserInputRequest(pendingRequest, &errorMessage)) {
        replyToThreadUiUserInputRequest(
            pendingRequest,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            errorMessage.isEmpty() ? QStringLiteral("Failed to send turn request.") : errorMessage
        );
        return;
    }
}

void LoadedThread::onThreadUiResolveLinkRequested(const std::uint64_t requestId, const QString &href) {
    const qodex::threadui::ipc::common::ResolvedLink resolvedLink = m_threadUiLinkPolicy.resolveLink(href, m_cwd);
    replyToThreadUiResolveLinkRequest(
        requestId,
        qodex::threadui::ipc::common::RESULT_STATUS_OK,
        QStringLiteral("Link resolved."),
        resolvedLink
    );
}

void LoadedThread::onTurnStartSucceeded(const JsonRpcId &id, const TurnStartResponse &response) {
    const auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    if (response.turn && !response.turn->id.isEmpty()) {
        const QString previousActiveTurnId = m_activeTurnId;
        m_activeTurnId = response.turn->id;
        setDerivedThreadStatus(
            QStringLiteral("active"),
            m_threadStatusKind == QStringLiteral("active") ? m_threadStatusText : QStringLiteral("Active"),
            m_threadActiveFlags
        );
        if (m_activeTurnId != previousActiveTurnId) {
            emit threadPresentationChanged();
        }
        queueThreadStatus();
    }

    replyToThreadUiUserInputRequest(
        pendingRequest,
        qodex::threadui::ipc::common::RESULT_STATUS_OK,
        QStringLiteral("Turn started.")
    );
}

void LoadedThread::onTurnStartFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    replyToThreadUiUserInputRequest(pendingRequest, qodex::threadui::ipc::common::RESULT_STATUS_ERROR, error.message);
}

void LoadedThread::onTurnSteerSucceeded(const JsonRpcId &id, const TurnSteerResponse &response) {
    const auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    if (!response.turnId.isEmpty()) {
        const QString previousActiveTurnId = m_activeTurnId;
        m_activeTurnId = response.turnId;
        setDerivedThreadStatus(
            QStringLiteral("active"),
            m_threadStatusKind == QStringLiteral("active") ? m_threadStatusText : QStringLiteral("Active"),
            m_threadActiveFlags
        );
        if (m_activeTurnId != previousActiveTurnId) {
            emit threadPresentationChanged();
        }
        queueThreadStatus();
    }

    replyToThreadUiUserInputRequest(
        pendingRequest,
        qodex::threadui::ipc::common::RESULT_STATUS_OK,
        QStringLiteral("Turn steered.")
    );
}

void LoadedThread::onTurnSteerFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    if (!pendingRequest.retriedAfterNoActiveTurnSteerFailure && isNoActiveTurnToSteerError(error)) {
        pendingRequest.dispatchKind = PendingThreadUiUserInputDispatchKind::TurnStart;
        pendingRequest.retriedAfterNoActiveTurnSteerFailure = true;
        const QString previousActiveTurnId = m_activeTurnId;
        m_activeTurnId.clear();
        setDerivedThreadStatus(QStringLiteral("idle"), QStringLiteral("Idle"));
        if (m_activeTurnId != previousActiveTurnId) {
            emit threadPresentationChanged();
        }
        queueThreadStatus();

        QString retryErrorMessage;
        if (requeuePendingThreadUiUserInputRequest(pendingRequest, &retryErrorMessage)) {
            return;
        }

        const QString failureMessage = retryErrorMessage.isEmpty()
            ? QStringLiteral("no active turn to steer")
            : QStringLiteral("%1 Retry as turn/start failed: %2").arg(error.message, retryErrorMessage);
        replyToThreadUiUserInputRequest(
            pendingRequest,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            failureMessage
        );
        return;
    }

    replyToThreadUiUserInputRequest(pendingRequest, qodex::threadui::ipc::common::RESULT_STATUS_ERROR, error.message);
}

JsonRpcId LoadedThread::dispatchPendingThreadUiUserInputRequest(const PendingThreadUiUserInputRequest &pendingRequest) const {
    const QList<Ref<UserInput>> input = buildTextUserInput(pendingRequest.text);
    if (pendingRequest.dispatchKind == PendingThreadUiUserInputDispatchKind::TurnSteer) {
        if (m_activeTurnId.isEmpty()) {
            return {};
        }

        return m_client->sendTurnSteerRequest(m_activeTurnId, input, m_threadId);
    }

    return m_client->sendTurnStartRequest(
        missing<std::variant<qodex::codex::AskForApprovalEnum, Ref<qodex::codex::AskForApprovalGranular>>>(),
        missing<qodex::codex::ApprovalsReviewer>(),
        missing<Ref<qodex::codex::CollaborationMode>>(),
        missing<QString>(),
        missing<qodex::codex::ReasoningEffort>(),
        input,
        missing<QString>(),
        std::nullopt,
        missing<qodex::codex::Personality>(),
        missing<Ref<qodex::codex::SandboxPolicy>>(),
        missing<qodex::codex::ServiceTier>(),
        missing<qodex::codex::ReasoningSummary>(),
        m_threadId
    );
}

bool LoadedThread::requeuePendingThreadUiUserInputRequest(
    PendingThreadUiUserInputRequest pendingRequest,
    QString *errorMessage
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const JsonRpcId transportRequestId = dispatchPendingThreadUiUserInputRequest(pendingRequest);
    if (!transportRequestId.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = pendingRequest.dispatchKind == PendingThreadUiUserInputDispatchKind::TurnSteer
                ? QStringLiteral("Failed to send turn/steer request.")
                : QStringLiteral("Failed to send turn/start request.");
        }
        return false;
    }

    m_pendingThreadUiUserInputRequests.insert(transportRequestId.toKey(), std::move(pendingRequest));
    return true;
}

void LoadedThread::replyToThreadUiUserInputRequest(
    const PendingThreadUiUserInputRequest &pendingRequest,
    const qodex::threadui::ipc::common::ResultStatus status,
    const QString &message
) {
    QString errorMessage;
    if (!m_threadUiProcess->replyToUserInputRequest(pendingRequest.requestId, status, message, &errorMessage)) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 input request: %2").arg(m_title, errorMessage)
        );
    }
}

void LoadedThread::replyToThreadUiResolveLinkRequest(
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const QString &message,
    const qodex::threadui::ipc::common::ResolvedLink &resolvedLink
) {
    QString errorMessage;
    if (!m_threadUiProcess->replyToResolveLinkRequest(requestId, status, message, resolvedLink, &errorMessage)) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 link request: %2").arg(m_title, errorMessage)
        );
    }
}

QString LoadedThread::activeTurnIdForThread(const Thread &thread) const {
    for (auto it = thread.turns.crbegin(); it != thread.turns.crend(); ++it) {
        const Ref<Turn> &turn = *it;
        if (!turn || turn->id.isEmpty()) {
            continue;
        }

        if (turn->status == TurnStatus::InProgress) {
            return turn->id;
        }
    }

    return {};
}

void LoadedThread::rebuildModelFromThread(const Thread &thread) {
    m_turnOrder.clear();
    m_turnsById.clear();

    for (const Ref<Turn> &turn : thread.turns) {
        if (!turn || turn->id.isEmpty()) {
            continue;
        }

        auto modelTurn = std::make_unique<qodex::domain::threadmodel::Turn>(turn->id);
        modelTurn->applySnapshot(*turn);
        m_turnOrder.append(turn->id);
        m_turnsById.emplace(turn->id, std::move(modelTurn));
    }
}

void LoadedThread::applyProtocolThreadStatus(const ThreadStatus &status) {
    QStringList activeFlags;
    setDerivedThreadStatus(threadStatusKindString(status), threadStatusText(status, &activeFlags), activeFlags);
}

void LoadedThread::setDerivedThreadStatus(
    const QString &kind,
    const QString &text,
    const QStringList &activeFlags
) {
    m_threadStatusKind = kind.trimmed().isEmpty() ? QStringLiteral("unknown") : kind.trimmed();
    m_threadStatusText = text.trimmed().isEmpty() ? QStringLiteral("Unknown") : text.trimmed();
    m_threadActiveFlags = activeFlags;
}

void LoadedThread::queueThreadStatus() {
    qodex::threadui::ipc::qodex_to_ui::SetThreadStatusRequest request;
    request.set_kind(m_threadStatusKind.toStdString());
    request.set_text(m_threadStatusText.toStdString());
    request.set_active_turn_id(m_activeTurnId.toStdString());
    for (const QString &activeFlag : m_threadActiveFlags) {
        request.add_active_flags(activeFlag.toStdString());
    }
    m_threadUiProcess->queueSetThreadStatus(request);
}

int LoadedThread::turnRow(const QString &turnId) const {
    return m_turnOrder.indexOf(turnId);
}

qodex::domain::threadmodel::Turn *LoadedThread::turnForIdMutable(const QString &turnId) {
    const auto it = m_turnsById.find(turnId);
    return it == m_turnsById.end() ? nullptr : it->second.get();
}

const qodex::domain::threadmodel::Turn *LoadedThread::turnForId(const QString &turnId) const {
    const auto it = m_turnsById.find(turnId);
    return it == m_turnsById.end() ? nullptr : it->second.get();
}

qodex::domain::threadmodel::Turn *LoadedThread::ensureTurn(const QString &turnId) {
    if (turnId.isEmpty()) {
        return nullptr;
    }

    if (auto *existingTurn = turnForIdMutable(turnId)) {
        return existingTurn;
    }

    auto turn = std::make_unique<qodex::domain::threadmodel::Turn>(turnId);
    qodex::domain::threadmodel::Turn *turnPtr = turn.get();
    m_turnOrder.append(turnId);
    m_turnsById.emplace(turnId, std::move(turn));
    return turnPtr;
}

void LoadedThread::queueDisplayItemIfSupported(const qodex::domain::threadmodel::AbstractItem &item) {
    const std::optional<qodex::threadui::ipc::common::Item> projectedItem = m_threadUiProjector.projectCompletedItem(item);
    if (!projectedItem.has_value()) {
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;
    *request.add_items() = *projectedItem;
    m_threadUiProcess->queueAddItems(request);
}

QList<Ref<UserInput>> LoadedThread::buildTextUserInput(const QString &text) const {
    QList<Ref<UserInput>> input;

    auto textInput = Ref<UserInputText>::create();
    textInput->text = text;

    auto userInput = Ref<UserInput>::create();
    userInput->kind = UserInput::Kind::Text;
    userInput->payload = textInput;

    input.append(userInput);
    return input;
}

}  // namespace qodex::app
