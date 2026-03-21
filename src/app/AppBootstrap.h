#pragma once

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
    [[nodiscard]] qodex::ui::MainWindow &secondaryWindow();
    void start();

private:
    AppPaths m_paths;
    AppConfig m_config;
    qodex::codex::AppServerTransport m_transport;
    qodex::codex::CodexClient m_client;
    qodex::domain::ThreadStore m_threadStore;
    qodex::ui::ThreadListModel m_threadListModel;
    qodex::ui::MainWindow m_mainWindow;
    qodex::ui::MainWindow m_secondaryWindow;
    SessionController m_sessionController;
};

}  // namespace qodex::app
