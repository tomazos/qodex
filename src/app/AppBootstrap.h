#pragma once

#include <memory>
#include <vector>

#include "app/AppConfig.h"
#include "app/AppPaths.h"
#include "domain/ThreadStore.h"
#include "ui/MainWindow.h"
#include "ui/ThreadListModel.h"
#include "codex/AppServerTransport.h"
#include "storage/DatabaseManager.h"

#include "CodexClient.h"
#include "app/SessionController.h"

namespace qodex::app {

class AppBootstrap final {
public:
    AppBootstrap(const AppPaths &paths, qodex::storage::DatabaseManager *databaseManager);

    [[nodiscard]] qodex::ui::MainWindow &mainWindow();
    void activate();
    void start();

private:
    [[nodiscard]] static QString titleForWindowKey(const QString &windowKey);
    [[nodiscard]] static QString threadListViewKeyForWindowKey(const QString &windowKey);
    [[nodiscard]] qodex::ui::MainWindow *windowByKey(const QString &windowKey);
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
    void rebuildWindowMenus();

    AppPaths m_paths;
    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    AppConfig m_config;
    qodex::codex::AppServerTransport m_transport;
    qodex::codex::CodexClient m_client;
    qodex::domain::ThreadStore m_threadStore;
    qodex::ui::ThreadListModel m_threadListModel;
    qodex::ui::MainWindow m_mainWindow;
    std::vector<std::unique_ptr<qodex::ui::MainWindow>> m_additionalWindows;
    SessionController m_sessionController;
    int m_nextWindowNumber = 2;
};

}  // namespace qodex::app
