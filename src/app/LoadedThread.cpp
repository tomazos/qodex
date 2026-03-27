#include "app/LoadedThread.h"

#include "app/ThreadUiProcess.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedItem.h"

namespace qodex::app {

namespace {

constexpr qint64 kJsonRpcInvalidRequestErrorCode = -32600;

bool isNoActiveTurnToSteerError(const qodex::codex::JsonRpcErrorObject &error) {
    return error.code == kJsonRpcInvalidRequestErrorCode &&
        error.message.trimmed().compare(QStringLiteral("no active turn to steer"), Qt::CaseInsensitive) == 0;
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
    qodex::codex::CodexClient *client,
    ThreadUiProcess *threadUiProcess,
    QObject *parent
)
    : QObject(parent),
      m_threadId(threadId),
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
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
    if (response.thread) {
        rebuildModelFromThread(*response.thread);
    }
    m_threadUiProcess->relaunch(m_title);
    m_threadUiProcess->queueAddItems(m_threadUiProjector.projectTurns(orderedTurns()));
    emit stateChanged();
}

void LoadedThread::onThreadClosed() {
    m_cwd.clear();
    m_activeTurnId.clear();
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
    emit stateChanged();
}

void LoadedThread::onThreadStatusChanged(const ThreadStatus &status) {
    if (status.kind != ThreadStatus::Kind::Active) {
        m_activeTurnId.clear();
    }
    emit stateChanged();
}

void LoadedThread::onTurnStartedNotification(const TurnStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.turn || params.turn->id.isEmpty()) {
        return;
    }

    ensureTurn(params.turn->id)->applyMetadata(*params.turn);
    m_activeTurnId = params.turn->id;
    emit stateChanged();
}

void LoadedThread::onTurnCompletedNotification(const TurnCompletedNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    if (params.turn && !params.turn->id.isEmpty()) {
        ensureTurn(params.turn->id)->applyMetadata(*params.turn);
        if (m_activeTurnId.isEmpty() || m_activeTurnId == params.turn->id) {
            m_activeTurnId.clear();
        }
        emit stateChanged();
        return;
    }

    m_activeTurnId.clear();
    emit stateChanged();
}

void LoadedThread::onItemStartedNotification(const qodex::codex::ItemStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    const auto *startedItem = ensureTurn(params.turnId)->applyStartedItem(*params.item);
    Q_UNUSED(startedItem);
    emit stateChanged();
}

void LoadedThread::onItemCompletedNotification(const qodex::codex::ItemCompletedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    if (auto *item = ensureTurn(params.turnId)->applyCompletedItem(*params.item)) {
        queueDisplayItemIfSupported(*item);
    }
    emit stateChanged();
}

void LoadedThread::onItemAgentMessageDeltaNotification(
    const qodex::codex::ItemAgentMessageDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyAgentMessageDelta(params.itemId, params.delta);
    emit stateChanged();
}

void LoadedThread::onItemCommandExecutionOutputDeltaNotification(
    const qodex::codex::ItemCommandExecutionOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyCommandExecutionOutputDelta(params.itemId, params.delta);
    emit stateChanged();
}

void LoadedThread::onItemCommandExecutionTerminalInteractionNotification(
    const qodex::codex::ItemCommandExecutionTerminalInteractionNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyCommandExecutionTerminalInteraction(params.itemId, params.processId, params.stdin);
    emit stateChanged();
}

void LoadedThread::onItemFileChangeOutputDeltaNotification(
    const qodex::codex::ItemFileChangeOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyFileChangeOutputDelta(params.itemId, params.delta);
    emit stateChanged();
}

void LoadedThread::onItemMcpToolCallProgressNotification(
    const qodex::codex::ItemMcpToolCallProgressNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyMcpToolCallProgress(params.itemId, params.message);
    emit stateChanged();
}

void LoadedThread::onItemPlanDeltaNotification(const qodex::codex::ItemPlanDeltaNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyPlanDelta(params.itemId, params.delta);
    emit stateChanged();
}

void LoadedThread::onItemReasoningSummaryPartAddedNotification(
    const qodex::codex::ItemReasoningSummaryPartAddedNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningSummaryPartAdded(params.itemId, params.summaryIndex);
    emit stateChanged();
}

void LoadedThread::onItemReasoningSummaryTextDeltaNotification(
    const qodex::codex::ItemReasoningSummaryTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningSummaryTextDelta(params.itemId, params.summaryIndex, params.delta);
    emit stateChanged();
}

void LoadedThread::onItemReasoningTextDeltaNotification(
    const qodex::codex::ItemReasoningTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningTextDelta(params.itemId, params.contentIndex, params.delta);
    emit stateChanged();
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
        m_activeTurnId = response.turn->id;
    }
    emit stateChanged();

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
        m_activeTurnId = response.turnId;
    }
    emit stateChanged();

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
        m_activeTurnId.clear();
        emit stateChanged();

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

qodex::domain::threadmodel::Turn *LoadedThread::turnForId(const QString &turnId) {
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

    if (auto *existingTurn = turnForId(turnId)) {
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
