#include "app/LoadedThread.h"

#include "app/ThreadUiProcess.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedAgentMessage.h"
#include "domain/threadmodel/CompletedUserMessage.h"

namespace qodex::app {

using qodex::codex::JsonRpcErrorObject;
using qodex::codex::JsonRpcId;
using qodex::codex::Nullable;
using qodex::codex::Ref;
using qodex::codex::Thread;
using qodex::codex::ThreadResumeResponse;
using qodex::codex::ThreadStatus;
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
}

void LoadedThread::onThreadClosed() {
    m_activeTurnId.clear();
    m_pendingThreadUiUserInputRequests.clear();
    m_turnOrder.clear();
    m_turnsById.clear();
}

void LoadedThread::onThreadStatusChanged(const ThreadStatus &status) {
    if (status.kind != ThreadStatus::Kind::Active) {
        m_activeTurnId.clear();
    }
}

void LoadedThread::onTurnStartedNotification(const TurnStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.turn || params.turn->id.isEmpty()) {
        return;
    }

    ensureTurn(params.turn->id)->applyMetadata(*params.turn);
    m_activeTurnId = params.turn->id;
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
        return;
    }

    m_activeTurnId.clear();
}

void LoadedThread::onItemStartedNotification(const qodex::codex::ItemStartedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    const auto *startedItem = ensureTurn(params.turnId)->applyStartedItem(*params.item);
    Q_UNUSED(startedItem);
}

void LoadedThread::onItemCompletedNotification(const qodex::codex::ItemCompletedNotificationParams &params) {
    if (params.threadId != m_threadId || !params.item || params.turnId.isEmpty()) {
        return;
    }

    if (auto *item = ensureTurn(params.turnId)->applyCompletedItem(*params.item)) {
        queueDisplayItemIfSupported(*item);
    }
}

void LoadedThread::onItemAgentMessageDeltaNotification(
    const qodex::codex::ItemAgentMessageDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyAgentMessageDelta(params.itemId, params.delta);
}

void LoadedThread::onItemCommandExecutionOutputDeltaNotification(
    const qodex::codex::ItemCommandExecutionOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyCommandExecutionOutputDelta(params.itemId, params.delta);
}

void LoadedThread::onItemCommandExecutionTerminalInteractionNotification(
    const qodex::codex::ItemCommandExecutionTerminalInteractionNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyCommandExecutionTerminalInteraction(params.itemId, params.processId, params.stdin);
}

void LoadedThread::onItemFileChangeOutputDeltaNotification(
    const qodex::codex::ItemFileChangeOutputDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyFileChangeOutputDelta(params.itemId, params.delta);
}

void LoadedThread::onItemMcpToolCallProgressNotification(
    const qodex::codex::ItemMcpToolCallProgressNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyMcpToolCallProgress(params.itemId, params.message);
}

void LoadedThread::onItemPlanDeltaNotification(const qodex::codex::ItemPlanDeltaNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyPlanDelta(params.itemId, params.delta);
}

void LoadedThread::onItemReasoningSummaryPartAddedNotification(
    const qodex::codex::ItemReasoningSummaryPartAddedNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningSummaryPartAdded(params.itemId, params.summaryIndex);
}

void LoadedThread::onItemReasoningSummaryTextDeltaNotification(
    const qodex::codex::ItemReasoningSummaryTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningSummaryTextDelta(params.itemId, params.summaryIndex, params.delta);
}

void LoadedThread::onItemReasoningTextDeltaNotification(
    const qodex::codex::ItemReasoningTextDeltaNotificationParams &params
) {
    if (params.threadId != m_threadId) {
        return;
    }

    ensureTurn(params.turnId)->applyReasoningTextDelta(params.itemId, params.contentIndex, params.delta);
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
            if (const auto *userMessage = dynamic_cast<const CompletedUserMessage *>(item)) {
                const QString text = flattenUserMessageContent(userMessage->data().content);
                if (text.trimmed().isEmpty()) {
                    continue;
                }

                request.add_items()->mutable_user_message()->set_text(text.toStdString());
                continue;
            }

            if (const auto *agentMessage = dynamic_cast<const CompletedAgentMessage *>(item)) {
                if (agentMessage->data().text.trimmed().isEmpty()) {
                    continue;
                }

                request.add_items()->mutable_agent_message()->set_text(agentMessage->data().text.toStdString());
            }
        }
    }

    return request;
}

void LoadedThread::queueDisplayItemIfSupported(const qodex::domain::threadmodel::AbstractItem &item) {
    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;

    if (const auto *userMessage = dynamic_cast<const CompletedUserMessage *>(&item)) {
        const QString text = flattenUserMessageContent(userMessage->data().content);
        if (!text.trimmed().isEmpty()) {
            request.add_items()->mutable_user_message()->set_text(text.toStdString());
        }
    } else if (const auto *agentMessage = dynamic_cast<const CompletedAgentMessage *>(&item)) {
        if (!agentMessage->data().text.trimmed().isEmpty()) {
            request.add_items()->mutable_agent_message()->set_text(agentMessage->data().text.toStdString());
        }
    }

    if (request.items_size() > 0) {
        m_threadUiProcess->queueAddItems(request);
    }
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
