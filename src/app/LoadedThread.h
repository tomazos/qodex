#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

#include <cstdint>

#include "CodexClient.h"
#include "CodexProtocol.h"
#include "domain/threadmodel/Turn.h"
#include "qodex_to_ui.pb.h"

namespace qodex::app {

class ThreadUiProcess;

class LoadedThread final : public QObject {
    Q_OBJECT

public:
    LoadedThread(
        const QString &threadId,
        qodex::codex::CodexClient *client,
        ThreadUiProcess *threadUiProcess,
        QObject *parent = nullptr
    );

    [[nodiscard]] QString threadId() const;
    [[nodiscard]] const QString &title() const;
    [[nodiscard]] const QString &activeTurnId() const;
    [[nodiscard]] QList<const qodex::domain::threadmodel::Turn *> orderedTurns() const;
    void resume(const QString &title, const qodex::codex::ThreadResumeResponse &response);
    void onThreadClosed();
    void onThreadStatusChanged(const qodex::codex::ThreadStatus &status);
    void onTurnStartedNotification(const qodex::codex::TurnStartedNotificationParams &params);
    void onTurnCompletedNotification(const qodex::codex::TurnCompletedNotificationParams &params);
    void onItemStartedNotification(const qodex::codex::ItemStartedNotificationParams &params);
    void onItemCompletedNotification(const qodex::codex::ItemCompletedNotificationParams &params);
    void onItemAgentMessageDeltaNotification(const qodex::codex::ItemAgentMessageDeltaNotificationParams &params);
    void onItemCommandExecutionOutputDeltaNotification(
        const qodex::codex::ItemCommandExecutionOutputDeltaNotificationParams &params
    );
    void onItemCommandExecutionTerminalInteractionNotification(
        const qodex::codex::ItemCommandExecutionTerminalInteractionNotificationParams &params
    );
    void onItemFileChangeOutputDeltaNotification(const qodex::codex::ItemFileChangeOutputDeltaNotificationParams &params);
    void onItemMcpToolCallProgressNotification(const qodex::codex::ItemMcpToolCallProgressNotificationParams &params);
    void onItemPlanDeltaNotification(const qodex::codex::ItemPlanDeltaNotificationParams &params);
    void onItemReasoningSummaryPartAddedNotification(
        const qodex::codex::ItemReasoningSummaryPartAddedNotificationParams &params
    );
    void onItemReasoningSummaryTextDeltaNotification(
        const qodex::codex::ItemReasoningSummaryTextDeltaNotificationParams &params
    );
    void onItemReasoningTextDeltaNotification(const qodex::codex::ItemReasoningTextDeltaNotificationParams &params);

signals:
    void statusMessageRequested(const QString &message);
    void stateChanged();

private slots:
    void onThreadUiUserInputRequested(std::uint64_t requestId, const QString &text);
    void onTurnStartSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::TurnStartResponse &response);
    void onTurnStartFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onTurnSteerSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::TurnSteerResponse &response);
    void onTurnSteerFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);

private:
    struct PendingThreadUiUserInputRequest {
        std::uint64_t requestId = 0;
    };

    [[nodiscard]] QString activeTurnIdForThread(const qodex::codex::Thread &thread) const;
    void rebuildModelFromThread(const qodex::codex::Thread &thread);
    [[nodiscard]] qodex::domain::threadmodel::Turn *turnForId(const QString &turnId);
    [[nodiscard]] const qodex::domain::threadmodel::Turn *turnForId(const QString &turnId) const;
    [[nodiscard]] qodex::domain::threadmodel::Turn *ensureTurn(const QString &turnId);
    [[nodiscard]] qodex::threadui::ipc::qodex_to_ui::AddItemsRequest buildThreadUiAddItemsRequest() const;
    void queueDisplayItemIfSupported(const qodex::domain::threadmodel::AbstractItem &item);
    [[nodiscard]] QList<qodex::codex::Ref<qodex::codex::UserInput>> buildTextUserInput(const QString &text) const;
    [[nodiscard]] QString flattenUserMessageContent(const QList<qodex::codex::Ref<qodex::codex::UserInput>> &content)
        const;
    template <typename T>
    [[nodiscard]] static qodex::codex::Nullable<T> missing() {
        return qodex::codex::Nullable<T>::missing();
    }

    QString m_threadId;
    QString m_title;
    QString m_activeTurnId;
    qodex::codex::CodexClient *m_client = nullptr;
    ThreadUiProcess *m_threadUiProcess = nullptr;
    QHash<QString, PendingThreadUiUserInputRequest> m_pendingThreadUiUserInputRequests;
    QStringList m_turnOrder;
    std::map<QString, std::unique_ptr<qodex::domain::threadmodel::Turn>> m_turnsById;
};

}  // namespace qodex::app
