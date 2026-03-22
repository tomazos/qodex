#include "app/AppBootstrap.h"

namespace qodex::app {

AppBootstrap::AppBootstrap(const AppPaths &paths)
    : m_paths(paths),
      m_config(AppConfig::loadDefault(m_paths)),
      m_transport(),
      m_client(&m_transport),
      m_threadStore(),
      m_threadListModel(&m_threadStore),
      m_mainWindow(
          QStringLiteral("qodex.MainWindow.Primary"),
          QStringLiteral("Qodex"),
          &m_threadListModel
      ),
      m_sessionController(m_config, &m_transport, &m_client, &m_threadStore, &m_mainWindow) {
    m_mainWindow.move(80, 80);
    QObject::connect(&m_mainWindow, &qodex::ui::MainWindow::createNewWindowRequested, [&] { createNewWindow(); });
    rebuildWindowMenus();
}

qodex::ui::MainWindow &AppBootstrap::mainWindow() {
    return m_mainWindow;
}

void AppBootstrap::activate() {
    m_mainWindow.setWindowState(m_mainWindow.windowState() & ~Qt::WindowMinimized);
    m_mainWindow.show();
    m_mainWindow.raise();
    m_mainWindow.activateWindow();
}

void AppBootstrap::start() {
    m_sessionController.start();
}

void AppBootstrap::createNewWindow() {
    auto window = std::make_unique<qodex::ui::MainWindow>(
        QStringLiteral("qodex.MainWindow.%1").arg(m_nextWindowNumber),
        QStringLiteral("Qodex %1").arg(m_nextWindowNumber),
        &m_threadListModel
    );
    window->move(80 + 40 * (m_nextWindowNumber - 1), 80 + 40 * (m_nextWindowNumber - 1));
    QObject::connect(window.get(), &qodex::ui::MainWindow::createNewWindowRequested, [&] { createNewWindow(); });
    m_sessionController.attachWindow(window.get());
    window->show();
    m_additionalWindows.push_back(std::move(window));
    ++m_nextWindowNumber;
    rebuildWindowMenus();
}

void AppBootstrap::rebuildWindowMenus() {
    QList<qodex::ui::MainWindow *> windows;
    windows.append(&m_mainWindow);
    for (const std::unique_ptr<qodex::ui::MainWindow> &window : m_additionalWindows) {
        windows.append(window.get());
    }

    m_mainWindow.rebuildWindowMenu(windows);
    for (const std::unique_ptr<qodex::ui::MainWindow> &window : m_additionalWindows) {
        window->rebuildWindowMenu(windows);
    }
}

}  // namespace qodex::app
