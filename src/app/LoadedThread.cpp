#include "app/LoadedThread.h"

#include "app/ThreadUiProcess.h"

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
    m_threadUiProcess->relaunch(m_title);
    m_threadUiProcess->queueAddItems(buildThreadUiAddItemsRequest(response));
}

void LoadedThread::onThreadClosed() {
    m_activeTurnId.clear();
    m_pendingThreadUiUserInputRequests.clear();
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

    m_activeTurnId = params.turn->id;
}

void LoadedThread::onTurnCompletedNotification(const TurnCompletedNotificationParams &params) {
    if (params.threadId != m_threadId) {
        return;
    }

    if (params.turn && !params.turn->id.isEmpty()) {
        if (m_activeTurnId.isEmpty() || m_activeTurnId == params.turn->id) {
            m_activeTurnId.clear();
        }
        return;
    }

    m_activeTurnId.clear();
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

qodex::threadui::ipc::qodex_to_ui::AddItemsRequest LoadedThread::buildThreadUiAddItemsRequest(
    const ThreadResumeResponse &response
) const {
    qodex::threadui::ipc::qodex_to_ui::AddItemsRequest request;
    if (!response.thread) {
        return request;
    }

    for (const Ref<Turn> &turn : response.thread->turns) {
        if (!turn) {
            continue;
        }

        for (const Ref<qodex::codex::ThreadItem> &item : turn->items) {
            if (!item) {
                continue;
            }

            switch (item->kind) {
            case qodex::codex::ThreadItem::Kind::UserMessage: {
                const Ref<qodex::codex::ThreadItemUserMessage> userMessage =
                    std::get<Ref<qodex::codex::ThreadItemUserMessage>>(item->payload);
                if (!userMessage) {
                    break;
                }

                const QString text = flattenUserMessageContent(userMessage->content);
                if (text.trimmed().isEmpty()) {
                    break;
                }

                request.add_items()->mutable_user_message()->set_text(text.toStdString());
                break;
            }
            case qodex::codex::ThreadItem::Kind::AgentMessage: {
                const Ref<qodex::codex::ThreadItemAgentMessage> agentMessage =
                    std::get<Ref<qodex::codex::ThreadItemAgentMessage>>(item->payload);
                if (!agentMessage || agentMessage->text.trimmed().isEmpty()) {
                    break;
                }

                request.add_items()->mutable_agent_message()->set_text(agentMessage->text.toStdString());
                break;
            }
            default:
                break;
            }
        }
    }

    return request;
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
