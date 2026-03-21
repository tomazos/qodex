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
          &m_threadListModel
      ),
      m_sessionController(m_config, &m_transport, &m_client, &m_threadStore, &m_mainWindow) {
    m_mainWindow.move(80, 80);
}

qodex::ui::MainWindow &AppBootstrap::mainWindow() {
    return m_mainWindow;
}

void AppBootstrap::start() {
    m_sessionController.start();
}

}  // namespace qodex::app
