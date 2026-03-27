#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

#include <cstdint>

#include "CodexClient.h"
#include "domain/ThreadUiLinkPolicy.h"
#include "CodexProtocol.h"
#include "domain/ThreadUiProjector.h"
#include "domain/threadmodel/Turn.h"
#include "qodex_to_ui.pb.h"

namespace qodex::app {

class ThreadUiProcess;

class LoadedThread final : public QObject {
    Q_OBJECT

public:
    LoadedThread(
        const QString &threadId,
        const QString &initialTitle,
        qodex::codex::CodexClient *client,
        ThreadUiProcess *threadUiProcess,
        QObject *parent = nullptr
    );

    [[nodiscard]] QString threadId() const;
    [[nodiscard]] const QString &title() const;
    [[nodiscard]] const QString &activeTurnId() const;
    [[nodiscard]] QList<const qodex::domain::threadmodel::Turn *> orderedTurns() const;
    [[nodiscard]] const qodex::domain::threadmodel::Turn *turnForId(const QString &turnId) const;
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
    void snapshotRebuilt();
    void threadPresentationChanged();
    void turnInserted(const QString &turnId, int row);
    void turnChanged(const QString &turnId);
    void itemInserted(const QString &turnId, const QString &itemId, int row);
    void itemChanged(const QString &turnId, const QString &itemId);

private slots:
    void onThreadUiUserInputRequested(std::uint64_t requestId, const QString &text);
    void onThreadUiResolveLinkRequested(std::uint64_t requestId, const QString &href);
    void onTurnStartSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::TurnStartResponse &response);
    void onTurnStartFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onTurnSteerSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::TurnSteerResponse &response);
    void onTurnSteerFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);

private:
    enum class PendingThreadUiUserInputDispatchKind {
        TurnStart,
        TurnSteer,
    };

    struct PendingThreadUiUserInputRequest {
        std::uint64_t requestId = 0;
        QString text;
        PendingThreadUiUserInputDispatchKind dispatchKind = PendingThreadUiUserInputDispatchKind::TurnStart;
        bool retriedAfterNoActiveTurnSteerFailure = false;
    };

    [[nodiscard]] QString activeTurnIdForThread(const qodex::codex::Thread &thread) const;
    void rebuildModelFromThread(const qodex::codex::Thread &thread);
    [[nodiscard]] int turnRow(const QString &turnId) const;
    [[nodiscard]] qodex::domain::threadmodel::Turn *turnForIdMutable(const QString &turnId);
    [[nodiscard]] qodex::domain::threadmodel::Turn *ensureTurn(const QString &turnId);
    [[nodiscard]] qodex::codex::JsonRpcId dispatchPendingThreadUiUserInputRequest(
        const PendingThreadUiUserInputRequest &pendingRequest
    ) const;
    [[nodiscard]] bool requeuePendingThreadUiUserInputRequest(
        PendingThreadUiUserInputRequest pendingRequest,
        QString *errorMessage = nullptr
    );
    void replyToThreadUiUserInputRequest(
        const PendingThreadUiUserInputRequest &pendingRequest,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message
    );
    void replyToThreadUiResolveLinkRequest(
        std::uint64_t requestId,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message,
        const qodex::threadui::ipc::common::ResolvedLink &resolvedLink
    );
    void queueDisplayItemIfSupported(const qodex::domain::threadmodel::AbstractItem &item);
    [[nodiscard]] QList<qodex::codex::Ref<qodex::codex::UserInput>> buildTextUserInput(const QString &text) const;
    template <typename T>
    [[nodiscard]] static qodex::codex::Nullable<T> missing() {
        return qodex::codex::Nullable<T>::missing();
    }

    QString m_threadId;
    QString m_title;
    QString m_cwd;
    QString m_activeTurnId;
    qodex::codex::CodexClient *m_client = nullptr;
    ThreadUiProcess *m_threadUiProcess = nullptr;
    QHash<QString, PendingThreadUiUserInputRequest> m_pendingThreadUiUserInputRequests;
    QStringList m_turnOrder;
    std::map<QString, std::unique_ptr<qodex::domain::threadmodel::Turn>> m_turnsById;
    qodex::domain::ThreadUiProjector m_threadUiProjector;
    qodex::domain::ThreadUiLinkPolicy m_threadUiLinkPolicy;
};

}  // namespace qodex::app
