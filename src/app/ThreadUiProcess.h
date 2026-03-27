#pragma once

#include <optional>
#include <cstdint>

#include <QObject>
#include <QString>

#include "qodex_to_ui.pb.h"

class QProcess;

namespace qodex::threadui {
class ThreadUiIpcServer;
}

namespace qodex::app {

struct ThreadUiProcessInfo {
    int instanceId = 0;
    QString title;
    qint64 processId = 0;
};

class ThreadUiProcess final : public QObject {
    Q_OBJECT

public:
    ThreadUiProcess(
        int instanceId,
        const QString &threadId,
        const QString &title,
        const QString &threadUiAppDir,
        const QString &threadUiStartScriptPath,
        const QString &nodeProgram,
        qodex::threadui::ThreadUiIpcServer *threadUiIpcServer,
        QObject *parent = nullptr
    );
    ~ThreadUiProcess() override;

    [[nodiscard]] int instanceId() const;
    [[nodiscard]] QString threadId() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString launchToken() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isAuthenticated() const;
    [[nodiscard]] bool matchesLaunchToken(const QString &launchToken) const;
    [[nodiscard]] ThreadUiProcessInfo info() const;

    void relaunch(const QString &title);
    void terminate();
    [[nodiscard]] bool activate(QString *errorMessage = nullptr);
    void queueAddItems(const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request);
    void queueSetThreadStatus(const qodex::threadui::ipc::qodex_to_ui::SetThreadStatusRequest &request);
    [[nodiscard]] bool replyToUserInputRequest(
        std::uint64_t requestId,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message,
        QString *errorMessage = nullptr
    );
    [[nodiscard]] bool replyToResolveLinkRequest(
        std::uint64_t requestId,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message,
        const qodex::threadui::ipc::common::ResolvedLink &resolvedLink,
        QString *errorMessage = nullptr
    );

    void handleAuthenticated();
    void handleDisconnected();
    void handleUserInputRequested(std::uint64_t requestId, const QString &text);
    void handleResolveLinkRequested(std::uint64_t requestId, const QString &href);

signals:
    void activeStateChanged();
    void statusMessageRequested(const QString &message);
    void processExitedExternally(const QString &threadId);
    void userInputRequested(std::uint64_t requestId, const QString &text);
    void resolveLinkRequested(std::uint64_t requestId, const QString &href);

private:
    void startProcess();
    void resetProcess();
    void flushPendingAddItems();
    void flushPendingSetThreadStatus();
    void drainStandardOutput();
    void drainStandardError();
    [[nodiscard]] QString effectiveTitle() const;

    int m_instanceId = 0;
    QString m_threadId;
    QString m_title;
    QString m_launchToken;
    QString m_lastStandardError;
    QString m_threadUiAppDir;
    QString m_threadUiStartScriptPath;
    QString m_nodeProgram;
    qodex::threadui::ThreadUiIpcServer *m_threadUiIpcServer = nullptr;
    bool m_authenticated = false;
    QProcess *m_process = nullptr;
    std::optional<qodex::threadui::ipc::qodex_to_ui::AddItemsRequest> m_pendingAddItemsRequest;
    std::optional<qodex::threadui::ipc::qodex_to_ui::SetThreadStatusRequest> m_pendingSetThreadStatusRequest;
};

}  // namespace qodex::app
