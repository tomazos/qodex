#pragma once

#include <memory>
#include <vector>

#include "app/AppConfig.h"
#include "app/AppPaths.h"
#include "domain/ThreadStore.h"
#include "ui/MainWindow.h"
#include "ui/ThreadListModel.h"
#include "codex/AppServerTransport.h"

#include "CodexClient.h"
#include "app/SessionController.h"

namespace qodex::app {

class AppBootstrap final {
public:
    AppBootstrap();

    [[nodiscard]] qodex::ui::MainWindow &mainWindow();
    void activate();
    void start();

private:
    void createNewWindow();
    void rebuildWindowMenus();

    AppPaths m_paths;
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
