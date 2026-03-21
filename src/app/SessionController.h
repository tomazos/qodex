#pragma once

#include <QObject>
#include <QProcess>

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

    void start();

private slots:
    void onTransportStarted();
    void onInitializeSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::InitializeResponse &response);
    void onInitializeFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onThreadListSucceeded(const qodex::codex::JsonRpcId &id, const qodex::codex::ThreadListResponse &response);
    void onThreadListFailed(const qodex::codex::JsonRpcId &id, const qodex::codex::JsonRpcErrorObject &error);
    void onRefreshRequested();
    void onThreadSelected(const QString &threadId);
    void onTransportErrorOccurred(const QString &message);
    void onTransportProcessExited(int exitCode, QProcess::ExitStatus exitStatus);
    void onThreadStartedNotificationReceived(const qodex::codex::ThreadStartedNotificationParams &params);
    void onThreadNameUpdatedNotificationReceived(const qodex::codex::ThreadNameUpdatedNotificationParams &params);
    void onThreadStatusChangedNotificationReceived(const qodex::codex::ThreadStatusChangedNotificationParams &params);
    void onThreadArchivedNotificationReceived(const qodex::codex::ThreadArchivedNotificationParams &params);
    void onThreadUnarchivedNotificationReceived(const qodex::codex::ThreadUnarchivedNotificationParams &params);
    void refreshSelectedThreadUi();

private:
    [[nodiscard]] domain::ThreadSummary projectThreadSummary(const qodex::codex::Thread &thread) const;
    [[nodiscard]] QString threadStatusText(const qodex::codex::ThreadStatus &status) const;
    [[nodiscard]] QString threadDisplayTitle(const qodex::codex::Thread &thread) const;
    [[nodiscard]] QString formatThreadSummaryText(const domain::ThreadSummary &summary) const;

    template <typename T>
    [[nodiscard]] static qodex::codex::Nullable<T> missing() {
        return qodex::codex::Nullable<T>::missing();
    }

    void requestThreadList();

    AppConfig m_config;
    codex::AppServerTransport *m_transport = nullptr;
    codex::CodexClient *m_client = nullptr;
    domain::ThreadStore *m_threadStore = nullptr;
    ui::MainWindow *m_mainWindow = nullptr;
    bool m_startRequested = false;
    bool m_threadListRequestInFlight = false;
};

}  // namespace qodex::app
