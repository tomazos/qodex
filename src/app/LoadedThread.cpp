#include "app/LoadedThread.h"

#include <QJsonDocument>

#include "app/ThreadUiProcess.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedAgentMessage.h"
#include "domain/threadmodel/CompletedCommandExecution.h"
#include "domain/threadmodel/CompletedFileChange.h"
#include "domain/threadmodel/CompletedReasoning.h"
#include "domain/threadmodel/CompletedUserMessage.h"

namespace qodex::app {

namespace {

QString flattenReasoningTextForThreadUi(const qodex::codex::ThreadItemReasoning &reasoning) {
    const auto joinSections = [](const std::optional<QList<QString>> &sections) {
        QStringList nonEmptySections;
        if (sections.has_value()) {
            for (const QString &section : *sections) {
                const QString trimmedSection = section.trimmed();
                if (!trimmedSection.isEmpty()) {
                    nonEmptySections.append(trimmedSection);
                }
            }
        }
        return nonEmptySections.join(QStringLiteral("\n\n"));
    };

    const QString summaryText = joinSections(reasoning.summary);
    if (!summaryText.isEmpty()) {
        return summaryText;
    }

    return joinSections(reasoning.content);
}

std::string patchApplyStatusToString(const qodex::codex::PatchApplyStatus status) {
    switch (status) {
    case qodex::codex::PatchApplyStatus::InProgress:
        return "in_progress";
    case qodex::codex::PatchApplyStatus::Completed:
        return "completed";
    case qodex::codex::PatchApplyStatus::Failed:
        return "failed";
    case qodex::codex::PatchApplyStatus::Declined:
        return "declined";
    }

    return "unknown";
}

qodex::threadui::ipc::common::FileChangeKind toThreadUiFileChangeKind(const qodex::codex::Ref<qodex::codex::PatchChangeKind> &kind) {
    if (!kind) {
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UNSPECIFIED;
    }

    switch (kind->kind) {
    case qodex::codex::PatchChangeKind::Kind::Add:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_ADD;
    case qodex::codex::PatchChangeKind::Kind::Delete:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_DELETE;
    case qodex::codex::PatchChangeKind::Kind::Update:
        return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UPDATE;
    }

    return qodex::threadui::ipc::common::FILE_CHANGE_KIND_UNSPECIFIED;
}

std::string commandExecutionStatusToString(const qodex::codex::CommandExecutionStatus status) {
    switch (status) {
    case qodex::codex::CommandExecutionStatus::InProgress:
        return "in_progress";
    case qodex::codex::CommandExecutionStatus::Completed:
        return "completed";
    case qodex::codex::CommandExecutionStatus::Failed:
        return "failed";
    case qodex::codex::CommandExecutionStatus::Declined:
        return "declined";
    }

    return "unknown";
}

QString commandActionLabel(const qodex::codex::Ref<qodex::codex::CommandAction> &action) {
    if (!action) {
        return {};
    }

    switch (action->kind) {
    case qodex::codex::CommandAction::Kind::Read: {
        const qodex::codex::Ref<qodex::codex::CommandActionRead> readAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionRead>>(action->payload);
        if (!readAction) {
            return {};
        }
        return readAction->path.isEmpty()
            ? QStringLiteral("Read")
            : QStringLiteral("Read %1").arg(readAction->path);
    }
    case qodex::codex::CommandAction::Kind::ListFiles: {
        const qodex::codex::Ref<qodex::codex::CommandActionListFiles> listFilesAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionListFiles>>(action->payload);
        if (!listFilesAction || !listFilesAction->path.hasValue() || listFilesAction->path.value().isEmpty()) {
            return QStringLiteral("List files");
        }
        return QStringLiteral("List %1").arg(listFilesAction->path.value());
    }
    case qodex::codex::CommandAction::Kind::Search: {
        const qodex::codex::Ref<qodex::codex::CommandActionSearch> searchAction =
            std::get<qodex::codex::Ref<qodex::codex::CommandActionSearch>>(action->payload);
        if (!searchAction) {
            return {};
        }

        if (searchAction->query.hasValue() && !searchAction->query.value().isEmpty()) {
            return QStringLiteral("Search %1").arg(searchAction->query.value());
        }

        if (searchAction->path.hasValue() && !searchAction->path.value().isEmpty()) {
            return QStringLiteral("Search in %1").arg(searchAction->path.value());
        }

        return QStringLiteral("Search");
    }
    case qodex::codex::CommandAction::Kind::Unknown:
        return {};
    }

    return {};
}

bool appendCommandExecutionDisplayItem(
    qodex::threadui::ipc::common::CommandExecution *displayItem,
    const qodex::domain::threadmodel::CompletedCommandExecution &commandExecutionItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemCommandExecution &payload = commandExecutionItem.data();
    displayItem->set_command(payload.command.toStdString());
    displayItem->set_cwd(payload.cwd.toStdString());
    displayItem->set_status(commandExecutionStatusToString(payload.status));

    if (payload.exitCode.hasValue()) {
        displayItem->set_has_exit_code(true);
        displayItem->set_exit_code(payload.exitCode.value());
    }

    if (payload.durationMs.hasValue()) {
        displayItem->set_has_duration_ms(true);
        displayItem->set_duration_ms(payload.durationMs.value());
    }

    if (payload.processId.hasValue() && !payload.processId.value().isEmpty()) {
        displayItem->set_process_id(payload.processId.value().toStdString());
    }

    if (payload.aggregatedOutput.hasValue()) {
        displayItem->set_aggregated_output(payload.aggregatedOutput.value().toStdString());
    }

    for (const qodex::codex::Ref<qodex::codex::CommandAction> &action : payload.commandActions) {
        const QString label = commandActionLabel(action).trimmed();
        if (!label.isEmpty()) {
            displayItem->add_action_labels(label.toStdString());
        }
    }

    return !payload.command.isEmpty() || payload.aggregatedOutput.hasValue();
}

bool appendFileChangeDisplayItem(
    qodex::threadui::ipc::common::FileChange *displayItem,
    const qodex::domain::threadmodel::CompletedFileChange &fileChangeItem
) {
    if (displayItem == nullptr) {
        return false;
    }

    const qodex::codex::ThreadItemFileChange &payload = fileChangeItem.data();
    displayItem->set_status(patchApplyStatusToString(payload.status));

    for (const qodex::codex::Ref<qodex::codex::FileUpdateChange> &changeRef : payload.changes) {
        if (!changeRef) {
            continue;
        }

        qodex::threadui::ipc::common::FileChangeChange *displayChange = displayItem->add_changes();
        displayChange->set_path(changeRef->path.toStdString());
        displayChange->set_kind(toThreadUiFileChangeKind(changeRef->kind));
        displayChange->set_diff(changeRef->diff.toStdString());

        if (changeRef->kind && changeRef->kind->kind == qodex::codex::PatchChangeKind::Kind::Update &&
            std::holds_alternative<qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate>>(changeRef->kind->payload)) {
            const qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate> updateKind =
                std::get<qodex::codex::Ref<qodex::codex::PatchChangeKindUpdate>>(changeRef->kind->payload);
            if (updateKind && updateKind->movePath.hasValue() && !updateKind->movePath.value().isEmpty()) {
                displayChange->set_move_path(updateKind->movePath.value().toStdString());
            }
        }
    }

    return displayItem->changes_size() > 0;
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
using qodex::domain::threadmodel::CompletedAgentMessage;
using qodex::domain::threadmodel::CompletedCommandExecution;
using qodex::domain::threadmodel::CompletedFileChange;
using qodex::domain::threadmodel::CompletedUserMessage;

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
    m_activeTurnId = response.thread ? activeTurnIdForThread(*response.thread) : QString{};
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
    if (response.thread) {
        rebuildModelFromThread(*response.thread);
    }
    m_threadUiProcess->relaunch(m_title);
    m_threadUiProcess->queueAddItems(buildThreadUiAddItemsRequest());
    emit stateChanged();
}

void LoadedThread::onThreadClosed() {
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
        QString ignoredError;
        const bool responseSent = m_threadUiProcess->replyToUserInputRequest(
            requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            QStringLiteral("Input must not be empty."),
            &ignoredError
        );
        Q_UNUSED(responseSent);
        return;
    }

    const QList<Ref<UserInput>> input = buildTextUserInput(trimmedText);

    JsonRpcId transportRequestId;
    if (m_activeTurnId.isEmpty()) {
        transportRequestId = m_client->sendTurnStartRequest(
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
    } else {
        transportRequestId = m_client->sendTurnSteerRequest(m_activeTurnId, input, m_threadId);
    }

    if (!transportRequestId.isValid()) {
        QString ignoredError;
        const bool responseSent = m_threadUiProcess->replyToUserInputRequest(
            requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            QStringLiteral("Failed to send turn request."),
            &ignoredError
        );
        Q_UNUSED(responseSent);
        return;
    }

    m_pendingThreadUiUserInputRequests.insert(
        transportRequestId.toKey(),
        PendingThreadUiUserInputRequest{
            .requestId = requestId,
        }
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

    QString errorMessage;
    if (!m_threadUiProcess->replyToUserInputRequest(
            pendingRequest.requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_OK,
            QStringLiteral("Turn started."),
            &errorMessage
        )) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 input request: %2").arg(m_title, errorMessage)
        );
    }
}

void LoadedThread::onTurnStartFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    QString errorMessage;
    if (!m_threadUiProcess->replyToUserInputRequest(
            pendingRequest.requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            error.message,
            &errorMessage
        )) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 input request: %2").arg(m_title, errorMessage)
        );
    }
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

    QString errorMessage;
    if (!m_threadUiProcess->replyToUserInputRequest(
            pendingRequest.requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_OK,
            QStringLiteral("Turn steered."),
            &errorMessage
        )) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 input request: %2").arg(m_title, errorMessage)
        );
    }
}

void LoadedThread::onTurnSteerFailed(const JsonRpcId &id, const JsonRpcErrorObject &error) {
    const auto pendingRequest = m_pendingThreadUiUserInputRequests.take(id.toKey());
    if (pendingRequest.requestId == 0) {
        return;
    }

    QString errorMessage;
    if (!m_threadUiProcess->replyToUserInputRequest(
            pendingRequest.requestId,
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            error.message,
            &errorMessage
        )) {
        emit statusMessageRequested(
            QStringLiteral("Failed to reply to %1 input request: %2").arg(m_title, errorMessage)
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

qodex::threadui::ipc::qodex_to_ui::AddItemsRequest LoadedThread::buildThreadUiAddItemsRequest() const {
    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;

    for (const QString &turnId : m_turnOrder) {
        const qodex::domain::threadmodel::Turn *turn = turnForId(turnId);
        if (turn == nullptr) {
            continue;
        }

        for (const AbstractItem *item : turn->orderedItems()) {
            if (item != nullptr) {
                qodex::threadui::ipc::common::Item *displayItem = request.add_items();
                if (!appendDisplayItem(displayItem, *item)) {
                    request.mutable_items()->RemoveLast();
                }
            }
        }
    }

    return request;
}

void LoadedThread::queueDisplayItemIfSupported(const qodex::domain::threadmodel::AbstractItem &item) {
    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;
    if (!appendDisplayItem(request.add_items(), item)) {
        request.mutable_items()->RemoveLast();
    }

    if (request.items_size() > 0) {
        m_threadUiProcess->queueAddItems(request);
    }
}

bool LoadedThread::appendDisplayItem(qodex::threadui::ipc::common::Item *displayItem, const AbstractItem &item) const {
    if (displayItem == nullptr || !item.isCompleted()) {
        return false;
    }

    switch (item.kind()) {
    case ThreadItem::Kind::UserMessage: {
        const auto &userMessage = static_cast<const CompletedUserMessage &>(item);
        const QString text = flattenUserMessageContent(userMessage.data().content).trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_user_message()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::AgentMessage: {
        const auto &agentMessage = static_cast<const CompletedAgentMessage &>(item);
        const QString text = agentMessage.data().text.trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_agent_message()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::Plan:
        displayItem->mutable_plan()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::Reasoning: {
        const auto &reasoning = static_cast<const qodex::domain::threadmodel::CompletedReasoning &>(item);
        const QString text = flattenReasoningTextForThreadUi(reasoning.data()).trimmed();
        if (text.isEmpty()) {
            return false;
        }
        displayItem->mutable_reasoning()->set_text(text.toStdString());
        return true;
    }
    case ThreadItem::Kind::CommandExecution:
        return appendCommandExecutionDisplayItem(
            displayItem->mutable_command_execution(),
            static_cast<const CompletedCommandExecution &>(item)
        );
    case ThreadItem::Kind::FileChange:
        return appendFileChangeDisplayItem(displayItem->mutable_file_change(), static_cast<const CompletedFileChange &>(item));
    case ThreadItem::Kind::McpToolCall:
        displayItem->mutable_mcp_tool_call()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::DynamicToolCall:
        displayItem->mutable_dynamic_tool_call()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::CollabAgentToolCall:
        displayItem->mutable_collab_agent_tool_call()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::WebSearch:
        displayItem->mutable_web_search()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::ImageView:
        displayItem->mutable_image_view()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::ImageGeneration:
        displayItem->mutable_image_generation()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::EnteredReviewMode:
        displayItem->mutable_entered_review_mode()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::ExitedReviewMode:
        displayItem->mutable_exited_review_mode()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    case ThreadItem::Kind::ContextCompaction:
        displayItem->mutable_context_compaction()->set_text(summarizeNonMessageItemForThreadUi(item).toStdString());
        return true;
    }

    return false;
}

QString LoadedThread::summarizeNonMessageItemForThreadUi(const AbstractItem &item) const {
    QJsonObject properties = item.properties();
    properties.remove(QStringLiteral("id"));
    const QByteArray compactJson = QJsonDocument(properties).toJson(QJsonDocument::Compact);
    return compactJson.isEmpty() || compactJson == "{}" ? item.id() : QString::fromUtf8(compactJson);
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

QString LoadedThread::flattenUserMessageContent(const QList<Ref<UserInput>> &content) const {
    QStringList parts;
    parts.reserve(content.size());

    for (const Ref<UserInput> &input : content) {
        if (!input) {
            continue;
        }

        switch (input->kind) {
        case UserInput::Kind::Text: {
            const Ref<UserInputText> textInput = std::get<Ref<UserInputText>>(input->payload);
            if (textInput && !textInput->text.isEmpty()) {
                parts.append(textInput->text);
            }
            break;
        }
        default:
            break;
        }
    }

    return parts.join(QStringLiteral("\n\n"));
}

}  // namespace qodex::app
