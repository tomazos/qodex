#pragma once

#include <memory>
#include <vector>

#include <QList>
#include <QObject>
#include <QString>

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

public slots:
    void launchThreadUi();
    void activateThreadUi(int instanceId);

signals:
    void activeProcessesChanged();
    void statusMessageRequested(const QString &message);

private:
    struct ProcessRecord {
        int instanceId = 0;
        QString title;
        QString launchToken;
        QString lastStandardError;
        QProcess *process = nullptr;
    };

    [[nodiscard]] static QString resolveThreadUiAppDir();
    [[nodiscard]] static QString resolveThreadUiStartScriptPath(const QString &appDir);
    [[nodiscard]] static QString resolveNodeProgram();
    [[nodiscard]] ProcessRecord *recordForInstanceId(int instanceId);
    [[nodiscard]] const ProcessRecord *recordForInstanceId(int instanceId) const;
    void removeRecord(int instanceId);
    void drainStandardOutput(ProcessRecord *record);
    void drainStandardError(ProcessRecord *record);
    void terminateAllProcesses();

    QString m_threadUiAppDir;
    QString m_threadUiStartScriptPath;
    QString m_nodeProgram;
    qodex::threadui::ThreadUiIpcServer *m_threadUiIpcServer = nullptr;
    std::vector<std::unique_ptr<ProcessRecord>> m_processes;
    int m_nextInstanceId = 1;
};

}  // namespace qodex::app
