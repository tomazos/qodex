#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QList>
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

class ThreadUiProcessManager final : public QObject {
    Q_OBJECT

public:
    explicit ThreadUiProcessManager(qodex::threadui::ThreadUiIpcServer *threadUiIpcServer, QObject *parent = nullptr);
    ~ThreadUiProcessManager() override;

    [[nodiscard]] QList<ThreadUiProcessInfo> activeProcesses() const;
    void showResumedThread(
        const QString &threadId,
        const QString &title,
        const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &addItemsRequest
    );
    void replyToUserInputRequest(
        int instanceId,
        std::uint64_t requestId,
        qodex::threadui::ipc::common::ResultStatus status,
        const QString &message
    );

public slots:
    void activateThreadUi(int instanceId);

signals:
    void activeProcessesChanged();
    void statusMessageRequested(const QString &message);
    void userInputRequested(int instanceId, const QString &threadId, std::uint64_t requestId, const QString &text);

private:
    struct ProcessRecord {
        int instanceId = 0;
        QString threadId;
        QString title;
        QString launchToken;
        QString lastStandardError;
        bool authenticated = false;
        QProcess *process = nullptr;
        std::optional<qodex::threadui::ipc::qodex_to_ui::AddItemsRequest> pendingAddItemsRequest;
    };

    [[nodiscard]] static QString resolveThreadUiAppDir();
    [[nodiscard]] static QString resolveThreadUiStartScriptPath(const QString &appDir);
    [[nodiscard]] static QString resolveNodeProgram();
    [[nodiscard]] ProcessRecord *recordForInstanceId(int instanceId);
    [[nodiscard]] const ProcessRecord *recordForInstanceId(int instanceId) const;
    [[nodiscard]] ProcessRecord *recordForThreadId(const QString &threadId);
    [[nodiscard]] ProcessRecord *recordForLaunchToken(const QString &launchToken);
    void relaunchThreadUiForThread(const QString &threadId, const QString &title);
    void flushPendingAddItems(ProcessRecord *record);
    void removeRecord(int instanceId);
    void drainStandardOutput(ProcessRecord *record);
    void drainStandardError(ProcessRecord *record);
    void terminateAllProcesses();
    void terminateRecord(ProcessRecord *record);
    void onThreadUiAuthenticated(const QString &token);
    void onThreadUiDisconnected(const QString &token);
    void onSendUserInputRequested(const QString &token, std::uint64_t requestId, const QString &text);

private:
    QString m_threadUiAppDir;
    QString m_threadUiStartScriptPath;
    QString m_nodeProgram;
    qodex::threadui::ThreadUiIpcServer *m_threadUiIpcServer = nullptr;
    std::vector<std::unique_ptr<ProcessRecord>> m_processes;
    int m_nextInstanceId = 1;
};

}  // namespace qodex::app
