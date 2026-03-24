#pragma once

#include <memory>
#include <vector>

#include <QObject>

#include "app/AppConfig.h"
#include "app/AppPaths.h"
#include "app/ThreadUiProcessManager.h"
#include "codex/AppServerTransport.h"
#include "codex/TrafficLogger.h"
#include "domain/ThreadStore.h"
#include "storage/DatabaseManager.h"
#include "ui/ApiLogModel.h"
#include "ui/MainWindow.h"
#include "ui/ProgressSplashScreen.h"
#include "ui/ThreadListModel.h"

#include "CodexClient.h"
#include "app/SessionController.h"

namespace qodex::threadui {
class ThreadUiIpcServer;
}

namespace qodex::app {

class AppBootstrap final : public QObject {
    Q_OBJECT

public:
    AppBootstrap(
        const AppPaths &paths,
        qodex::storage::DatabaseManager *databaseManager,
        qodex::threadui::ThreadUiIpcServer *threadUiIpcServer
    );

    [[nodiscard]] qodex::ui::MainWindow &mainWindow();
    void showAllWindows();
    void hideAllWindows();
    void activate();
    void start();

signals:
    void startupProgressChanged(const QString &message, int progress);
    void startupFinished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] static QString titleForWindowKey(const QString &windowKey);
    [[nodiscard]] static QString threadListViewKeyForWindowKey(const QString &windowKey);
    [[nodiscard]] static QString apiLogViewKeyForWindowKey(const QString &windowKey);
    [[nodiscard]] qodex::ui::MainWindow *windowByKey(const QString &windowKey);
    [[nodiscard]] QList<qodex::ui::MainWindow *> windows();
    [[nodiscard]] int visibleWindowCount();
    void beginShutdown();
    void continueShutdown();
    void restorePersistentState();
    void savePersistentState(const qodex::ui::MainWindow *excludingWindow = nullptr);
    void restoreWindowViewState(qodex::ui::MainWindow *window);
    void onWindowAboutToClose(qodex::ui::MainWindow *window);
    qodex::ui::MainWindow *createWindow(
        const QString &windowKey,
        const QString &windowTitle,
        bool showImmediately
    );
    void createNewWindow();
    void rebuildThreadMenus();
    void rebuildViewMenus();
    void rebuildWindowMenus();

    AppPaths m_paths;
    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    AppConfig m_config;
    qodex::codex::AppServerTransport m_transport;
    qodex::codex::TrafficLogger m_trafficLogger;
    qodex::codex::CodexClient m_client;
    qodex::domain::ThreadStore m_threadStore;
    qodex::ui::ThreadListModel m_threadListModel;
    qodex::ui::ApiLogModel m_apiLogModel;
    qodex::ui::MainWindow m_mainWindow;
    std::vector<std::unique_ptr<qodex::ui::MainWindow>> m_additionalWindows;
    ThreadUiProcessManager m_threadUiProcessManager;
    SessionController m_sessionController;
    std::unique_ptr<qodex::ui::ProgressSplashScreen> m_shutdownSplash;
    bool m_shutdownInProgress = false;
    int m_nextWindowNumber = 2;
};

}  // namespace qodex::app
