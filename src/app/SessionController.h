#pragma once

#include <QObject>
#include <QProcess>
#include <QHash>
#include <QList>
#include <QStringList>

#include <utility>

#include "CodexProtocol.h"
#include "app/AppConfig.h"
#include "codex/AppServerTransport.h"
#include "CodexClient.h"
#include "codex/JsonRpcMessage.h"

namespace qodex::domain {
struct ThreadSummary;
class ThreadStore;
}

namespace qodex::ui {
class MainWindow;
}

namespace qodex::app {

class SessionController final : public QObject {
    Q_OBJECT

public:
    SessionController(
        const AppConfig &config,
        codex::AppServerTransport *transport,
        codex::CodexClient *client,
        domain::ThreadStore *threadStore,
        ui::MainWindow *mainWindow,
        QObject *parent = nullptr
    );

    void attachWindow(ui::MainWindow *window);
    void start();

signals:
    void startupProgressChanged(const QString &message, int progress);
    void startupFinished();

private slots:
    void onTransportStarted();
    void onInitializeSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::InitializeResponse &response);
    void onInitializeFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadListSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::ThreadListResponse &response);
    void onThreadListFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onRefreshRequested();
    void onThreadSelected(const QString &threadId);
    void onRenameThreadRequested(const QString &threadId);
    void onResumeThreadRequested(const QString &threadId);
    void onForkThreadRequested(const QString &threadId);
    void onArchiveThreadsRequested(const QStringList &threadIds);
    void onUnarchiveThreadsRequested(const QStringList &threadIds);
    void onTransportErrorOccurred(const QString &message);
    void onTransportProcessExited(int exitCode, QProcess::ExitStatus exitStatus);
    void onThreadNameSetSucceeded(const qodex::codex::JsonRpcId &id, qodex::codex::EmptyObject response);
    void onThreadNameSetFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadArchiveSucceeded(const qodex::codex::JsonRpcId &id, qodex::codex::EmptyObject response);
    void onThreadArchiveFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadResumeSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::ThreadResumeResponse &response);
    void onThreadResumeFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadForkSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::ThreadForkResponse &response);
    void onThreadForkFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadUnarchiveSucceeded(
        const qodex::codex::JsonRpcId &id,
        const qodex::codex::ThreadUnarchiveResponse &response
    );
    void onThreadUnarchiveFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadStartedNotificationReceived(const qodex::codex::ThreadStartedNotificationParams &params);
    void onThreadNameUpdatedNotificationReceived(const qodex::codex::ThreadNameUpdatedNotificationParams &params);
    void onThreadStatusChangedNotificationReceived(const qodex::codex::ThreadStatusChangedNotificationParams &params);
    void onThreadArchivedNotificationReceived(const qodex::codex::ThreadArchivedNotificationParams &params);
    void onThreadUnarchivedNotificationReceived(const qodex::codex::ThreadUnarchivedNotificationParams &params);
    void refreshSelectedThreadUi();

private:
    [[nodiscard]] domain::ThreadSummary projectThreadSummary(const qodex::codex::Thread &thread, bool archived) const;
    [[nodiscard]] QString threadStatusText(const qodex::codex::ThreadStatus &status) const;
    [[nodiscard]] QString threadDisplayTitle(const qodex::codex::Thread &thread) const;
    [[nodiscard]] QString threadSourceText(const qodex::codex::SessionSource &source) const;
    template <typename T>
    [[nodiscard]] static qodex::codex::Nullable<T> missing() {
        return qodex::codex::Nullable<T>::missing();
    }

    void finishStartup(const QString &message);
    void requestThreadLists();
    void requestThreadList(bool archived);

    AppConfig m_config;
    codex::AppServerTransport *m_transport = nullptr;
    codex::CodexClient *m_client = nullptr;
    domain::ThreadStore *m_threadStore = nullptr;
    ui::MainWindow *m_mainWindow = nullptr;
    QList<ui::MainWindow *> m_windows;
    bool m_startRequested = false;
    bool m_startupFinished = false;
    bool m_activeThreadListRequestInFlight = false;
    bool m_archivedThreadListRequestInFlight = false;
    QString m_activeThreadListRequestKey;
    QString m_archivedThreadListRequestKey;
    QHash<QString, std::pair<QString, QString>> m_pendingRenameRequests;
    QHash<QString, QString> m_pendingArchiveRequests;
    QHash<QString, QString> m_pendingResumeRequests;
    QHash<QString, QString> m_pendingForkRequests;
    QHash<QString, QString> m_pendingUnarchiveRequests;
};

}  // namespace qodex::app
