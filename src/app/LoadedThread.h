#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <cstdint>

#include "CodexClient.h"
#include "CodexProtocol.h"
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
    void resume(const QString &title, const qodex::codex::ThreadResumeResponse &response);
    void onThreadClosed();
    void onThreadStatusChanged(const qodex::codex::ThreadStatus &status);
    void onTurnStartedNotification(const qodex::codex::TurnStartedNotificationParams &params);
    void onTurnCompletedNotification(const qodex::codex::TurnCompletedNotificationParams &params);

signals:
    void statusMessageRequested(const QString &message);

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
    [[nodiscard]] qodex::threadui::ipc::qodex_to_ui::AddItemsRequest buildThreadUiAddItemsRequest(
        const qodex::codex::ThreadResumeResponse &response
    ) const;
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
};

}  // namespace qodex::app
