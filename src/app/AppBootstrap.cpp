#include "app/AppBootstrap.h"

#include <algorithm>

#include <QCoreApplication>
#include <QEvent>
#include <QRegularExpression>
#include <QTimer>
#include <kddockwidgets/LayoutSaver.h>

#include "ui/ApiLogPane.h"
#include "ui/ThreadListPane.h"

namespace qodex::app {

AppBootstrap::AppBootstrap(const AppPaths &paths, qodex::storage::DatabaseManager *databaseManager)
    : QObject(nullptr),
      m_paths(paths),
      m_databaseManager(databaseManager),
      m_config(AppConfig::loadDefault(m_paths)),
      m_transport(),
      m_trafficLogger(databaseManager, &m_transport),
      m_client(&m_transport),
      m_threadStore(),
      m_threadListModel(&m_threadStore),
      m_apiLogModel(m_databaseManager),
      m_mainWindow(
          QStringLiteral("qodex.MainWindow.Primary"),
          QStringLiteral("Qodex"),
          &m_threadListModel,
          &m_apiLogModel
      ),
      m_threadUiProcessManager(this),
      m_sessionController(m_config, &m_transport, &m_client, &m_threadStore, &m_mainWindow) {
    m_mainWindow.move(80, 80);
    m_mainWindow.installEventFilter(this);
    QObject::connect(&m_mainWindow, &qodex::ui::MainWindow::launchThreadUiRequested, &m_threadUiProcessManager, &ThreadUiProcessManager::launchThreadUi);
    QObject::connect(
        &m_mainWindow,
        &qodex::ui::MainWindow::activateThreadUiRequested,
        &m_threadUiProcessManager,
        &ThreadUiProcessManager::activateThreadUi
    );
    QObject::connect(&m_mainWindow, &qodex::ui::MainWindow::createNewWindowRequested, [&] { createNewWindow(); });
    QObject::connect(&m_mainWindow, &qodex::ui::MainWindow::quitRequested, this, &AppBootstrap::beginShutdown);
    QObject::connect(&m_mainWindow, &qodex::ui::MainWindow::aboutToClose, [&](qodex::ui::MainWindow *window) {
        onWindowAboutToClose(window);
    });
    QObject::connect(
        &m_threadUiProcessManager,
        &ThreadUiProcessManager::activeProcessesChanged,
        this,
        &AppBootstrap::rebuildThreadMenus
    );
    QObject::connect(
        &m_threadUiProcessManager,
        &ThreadUiProcessManager::statusMessageRequested,
        this,
        [this](const QString &message) {
            for (qodex::ui::MainWindow *window : windows()) {
                if (window != nullptr) {
                    window->setStatusMessage(message);
                }
            }
        }
    );
    QObject::connect(&m_trafficLogger, &qodex::codex::TrafficLogger::apiLogRecorded, &m_apiLogModel, &qodex::ui::ApiLogModel::scheduleRefresh);
    QObject::connect(
        &m_sessionController,
        &SessionController::startupProgressChanged,
        this,
        &AppBootstrap::startupProgressChanged
    );
    QObject::connect(&m_sessionController, &SessionController::startupFinished, this, &AppBootstrap::startupFinished);
    restorePersistentState();
    rebuildThreadMenus();
    rebuildViewMenus();
    rebuildWindowMenus();
}

qodex::ui::MainWindow &AppBootstrap::mainWindow() {
    return m_mainWindow;
}

void AppBootstrap::showAllWindows() {
    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr) {
            window->setProperty("_q_showWithoutActivating", true);
            window->show();
            window->setProperty("_q_showWithoutActivating", false);
        }
    }
}

void AppBootstrap::hideAllWindows() {
    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr) {
            window->hide();
        }
    }
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

bool AppBootstrap::eventFilter(QObject *watched, QEvent *event) {
    if (!m_shutdownInProgress && event != nullptr && event->type() == QEvent::Close) {
        for (qodex::ui::MainWindow *window : windows()) {
            if (window != watched) {
                continue;
            }
            if (visibleWindowCount() <= 1) {
                event->ignore();
                beginShutdown();
                return true;
            }
            break;
        }
    }

    return QObject::eventFilter(watched, event);
}

QString AppBootstrap::titleForWindowKey(const QString &windowKey) {
    if (windowKey == QStringLiteral("qodex.MainWindow.Primary")) {
        return QStringLiteral("Qodex");
    }

    static const QRegularExpression numberedWindowPattern(QStringLiteral(R"(qodex\.MainWindow\.(\d+))"));
    const QRegularExpressionMatch match = numberedWindowPattern.match(windowKey);
    if (match.hasMatch()) {
        return QStringLiteral("Qodex %1").arg(match.captured(1));
    }

    return QStringLiteral("Qodex");
}

QString AppBootstrap::threadListViewKeyForWindowKey(const QString &windowKey) {
    return QStringLiteral("%1/thread-list-header").arg(windowKey);
}

QString AppBootstrap::apiLogViewKeyForWindowKey(const QString &windowKey) {
    return QStringLiteral("%1/api-log-header").arg(windowKey);
}

qodex::ui::MainWindow *AppBootstrap::windowByKey(const QString &windowKey) {
    if (m_mainWindow.windowKey() == windowKey) {
        return &m_mainWindow;
    }

    for (const std::unique_ptr<qodex::ui::MainWindow> &window : m_additionalWindows) {
        if (window != nullptr && window->windowKey() == windowKey) {
            return window.get();
        }
    }
    return nullptr;
}

QList<qodex::ui::MainWindow *> AppBootstrap::windows() {
    QList<qodex::ui::MainWindow *> allWindows;
    allWindows.append(&m_mainWindow);
    for (const std::unique_ptr<qodex::ui::MainWindow> &window : m_additionalWindows) {
        if (window != nullptr) {
            allWindows.append(window.get());
        }
    }
    return allWindows;
}

int AppBootstrap::visibleWindowCount() {
    int count = 0;
    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr && window->isVisible()) {
            ++count;
        }
    }
    return count;
}

void AppBootstrap::beginShutdown() {
    if (m_shutdownInProgress) {
        return;
    }
    m_shutdownInProgress = true;

    m_shutdownSplash = std::make_unique<qodex::ui::ProgressSplashScreen>(QStringLiteral("Closing Qodex"));
    m_shutdownSplash->setStatus(QStringLiteral("Preparing to close Qodex..."), 10);
    m_shutdownSplash->showCentered();

    m_shutdownSplash->setStatus(QStringLiteral("Saving workspace state..."), 25);
    savePersistentState(nullptr);

    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr) {
            window->hide();
        }
    }

    m_shutdownSplash->setStatus(QStringLiteral("Stopping Codex app-server..."), 70);
    m_shutdownSplash->raise();
    m_shutdownSplash->activateWindow();
    QTimer::singleShot(0, this, &AppBootstrap::continueShutdown);
}

void AppBootstrap::continueShutdown() {
    if (m_transport.isRunning()) {
        m_transport.stop();
    }

    if (m_shutdownSplash) {
        m_shutdownSplash->setStatus(QStringLiteral("Closing Qodex..."), 100);
        m_shutdownSplash->raise();
        m_shutdownSplash->activateWindow();
        m_shutdownSplash->close();
    }

    QCoreApplication::quit();
}

void AppBootstrap::restorePersistentState() {
    if (m_databaseManager == nullptr || !m_databaseManager->isOpen()) {
        return;
    }

    QString errorMessage;
    const QList<qodex::storage::WindowStateRecord> windowStates = m_databaseManager->loadWindowStates(&errorMessage);
    if (!errorMessage.isEmpty()) {
        qWarning("Failed to load persisted window state: %s", qPrintable(errorMessage));
    }

    int highestWindowNumber = 1;
    for (const qodex::storage::WindowStateRecord &record : windowStates) {
        if (record.windowKey == m_mainWindow.windowKey()) {
            continue;
        }
        createWindow(record.windowKey, titleForWindowKey(record.windowKey), false);

        static const QRegularExpression numberedWindowPattern(QStringLiteral(R"(qodex\.MainWindow\.(\d+))"));
        const QRegularExpressionMatch match = numberedWindowPattern.match(record.windowKey);
        if (match.hasMatch()) {
            highestWindowNumber = std::max(highestWindowNumber, match.captured(1).toInt());
        }
    }
    m_nextWindowNumber = std::max(2, highestWindowNumber + 1);

    for (const qodex::storage::WindowStateRecord &record : windowStates) {
        qodex::ui::MainWindow *window = windowByKey(record.windowKey);
        if (window == nullptr) {
            continue;
        }
        restoreWindowViewState(window);
    }

    const auto primaryState = std::find_if(
        windowStates.begin(),
        windowStates.end(),
        [this](const qodex::storage::WindowStateRecord &record) { return record.windowKey == m_mainWindow.windowKey(); }
    );
    if (primaryState != windowStates.end() && !primaryState->layout.isEmpty()) {
        KDDockWidgets::LayoutSaver layoutSaver;
        if (!layoutSaver.restoreLayout(primaryState->layout)) {
            qWarning("Failed to restore persisted dock layout.");
        }
    }

    for (const qodex::storage::WindowStateRecord &record : windowStates) {
        qodex::ui::MainWindow *window = windowByKey(record.windowKey);
        if (window == nullptr) {
            continue;
        }
        if (!record.geometry.isEmpty()) {
            window->restoreGeometry(record.geometry);
        }
        if (window != &m_mainWindow) {
            window->show();
        }
    }
}

void AppBootstrap::savePersistentState(const qodex::ui::MainWindow *excludingWindow) {
    if (m_databaseManager == nullptr || !m_databaseManager->isOpen()) {
        return;
    }

    QList<qodex::storage::WindowStateRecord> windowStates;
    QList<qodex::storage::ViewStateRecord> viewStates;
    KDDockWidgets::LayoutSaver layoutSaver;
    const QByteArray serializedLayout = layoutSaver.serializeLayout();

    const auto collectWindowState = [&](qodex::ui::MainWindow *window, const bool includeLayout) {
        if (window == nullptr) {
            return;
        }
        if (window == excludingWindow) {
            return;
        }
        if (window != &m_mainWindow && !window->isVisible()) {
            return;
        }

        windowStates.append({
            window->windowKey(),
            window->saveGeometry(),
            includeLayout ? serializedLayout : QByteArray{},
        });

        if (qodex::ui::ThreadListPane *pane = window->threadListPane()) {
            viewStates.append(qodex::storage::ViewStateRecord{
                threadListViewKeyForWindowKey(window->windowKey()),
                pane->saveHeaderState(),
            });
        }
        if (qodex::ui::ApiLogPane *pane = window->apiLogPane()) {
            viewStates.append(qodex::storage::ViewStateRecord{
                apiLogViewKeyForWindowKey(window->windowKey()),
                pane->saveViewState(),
            });
        }
    };

    collectWindowState(&m_mainWindow, true);
    for (const std::unique_ptr<qodex::ui::MainWindow> &window : m_additionalWindows) {
        collectWindowState(window.get(), false);
    }

    QString errorMessage;
    if (!m_databaseManager->replaceWindowStates(windowStates, &errorMessage)) {
        qWarning("Failed to save window state: %s", qPrintable(errorMessage));
    }

    errorMessage.clear();
    if (!m_databaseManager->replaceViewStates(viewStates, &errorMessage)) {
        qWarning("Failed to save view state: %s", qPrintable(errorMessage));
    }
}

void AppBootstrap::restoreWindowViewState(qodex::ui::MainWindow *window) {
    if (m_databaseManager == nullptr || window == nullptr) {
        return;
    }

    QString errorMessage;
    if (window->threadListPane() != nullptr) {
        const auto headerState =
            m_databaseManager->loadViewState(threadListViewKeyForWindowKey(window->windowKey()), &errorMessage);
        if (!errorMessage.isEmpty()) {
            qWarning("Failed to load thread list view state: %s", qPrintable(errorMessage));
            errorMessage.clear();
        } else if (headerState.has_value()) {
            window->threadListPane()->restoreHeaderState(*headerState);
        }
    }

    if (window->apiLogPane() != nullptr) {
        const auto headerState =
            m_databaseManager->loadViewState(apiLogViewKeyForWindowKey(window->windowKey()), &errorMessage);
        if (!errorMessage.isEmpty()) {
            qWarning("Failed to load API log view state: %s", qPrintable(errorMessage));
        } else if (headerState.has_value()) {
            window->apiLogPane()->restoreViewState(*headerState);
        }
    }
}

void AppBootstrap::onWindowAboutToClose(qodex::ui::MainWindow *window) {
    if (window == &m_mainWindow) {
        savePersistentState(nullptr);
        return;
    }

    savePersistentState(window);
}

qodex::ui::MainWindow *AppBootstrap::createWindow(
    const QString &windowKey,
    const QString &windowTitle,
    const bool showImmediately
) {
    auto window = std::make_unique<qodex::ui::MainWindow>(windowKey, windowTitle, nullptr, nullptr);
    window->move(80 + 40 * (m_nextWindowNumber - 1), 80 + 40 * (m_nextWindowNumber - 1));
    window->installEventFilter(this);
    QObject::connect(window.get(), &qodex::ui::MainWindow::launchThreadUiRequested, &m_threadUiProcessManager, &ThreadUiProcessManager::launchThreadUi);
    QObject::connect(
        window.get(),
        &qodex::ui::MainWindow::activateThreadUiRequested,
        &m_threadUiProcessManager,
        &ThreadUiProcessManager::activateThreadUi
    );
    QObject::connect(window.get(), &qodex::ui::MainWindow::createNewWindowRequested, [&] { createNewWindow(); });
    QObject::connect(window.get(), &qodex::ui::MainWindow::quitRequested, this, &AppBootstrap::beginShutdown);
    QObject::connect(window.get(), &qodex::ui::MainWindow::aboutToClose, [&](qodex::ui::MainWindow *closingWindow) {
        onWindowAboutToClose(closingWindow);
    });
    m_sessionController.attachWindow(window.get());
    qodex::ui::MainWindow *windowPtr = window.get();
    windowPtr->rebuildThreadMenu({});
    if (showImmediately) {
        windowPtr->show();
    }
    m_additionalWindows.push_back(std::move(window));
    return windowPtr;
}

void AppBootstrap::createNewWindow() {
    createWindow(
        QStringLiteral("qodex.MainWindow.%1").arg(m_nextWindowNumber),
        QStringLiteral("Qodex %1").arg(m_nextWindowNumber),
        true
    );
    ++m_nextWindowNumber;
    rebuildThreadMenus();
    rebuildViewMenus();
    rebuildWindowMenus();
}

void AppBootstrap::rebuildThreadMenus() {
    QList<qodex::ui::ThreadUiMenuEntry> entries;
    const QList<ThreadUiProcessInfo> processes = m_threadUiProcessManager.activeProcesses();
    entries.reserve(processes.size());

    for (const ThreadUiProcessInfo &process : processes) {
        entries.append({
            .instanceId = process.instanceId,
            .title = process.title,
        });
    }

    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr) {
            window->rebuildThreadMenu(entries);
        }
    }
}

void AppBootstrap::rebuildViewMenus() {
    const QList<QAction *> actions = m_mainWindow.viewActions();
    for (qodex::ui::MainWindow *window : windows()) {
        if (window != nullptr) {
            window->rebuildViewMenu(actions);
        }
    }
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
