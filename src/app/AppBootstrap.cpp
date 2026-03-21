#include "app/AppBootstrap.h"

namespace qodex::app {

AppBootstrap::AppBootstrap()
    : m_paths(AppPaths::discover()),
      m_config(AppConfig::loadDefault(m_paths)),
      m_transport(),
      m_client(&m_transport),
      m_threadStore(),
      m_threadListModel(&m_threadStore),
      m_mainWindow(
          QStringLiteral("qodex.MainWindow.Primary"),
          QStringLiteral("qodex"),
          &m_threadListModel,
          true
      ),
      m_secondaryWindow(
          QStringLiteral("qodex.MainWindow.Secondary"),
          QStringLiteral("qodex Secondary"),
          &m_threadListModel,
          false
      ),
      m_sessionController(m_config, &m_transport, &m_client, &m_threadStore, &m_mainWindow) {
    m_mainWindow.move(80, 80);
    m_secondaryWindow.move(760, 120);
}

qodex::ui::MainWindow &AppBootstrap::mainWindow() {
    return m_mainWindow;
}

qodex::ui::MainWindow &AppBootstrap::secondaryWindow() {
    return m_secondaryWindow;
}

void AppBootstrap::start() {
    m_sessionController.start();
}

}  // namespace qodex::app
