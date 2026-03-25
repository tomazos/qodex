#include "ui/MainWindow.h"

#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <kddockwidgets/DockWidget.h>

#include "ui/ApiLogModel.h"
#include "ui/ApiLogInspectorPane.h"
#include "ui/ApiLogPane.h"
#include "ui/LoadedThreadsModel.h"
#include "ui/LoadedThreadsPane.h"
#include "ui/ModelsModel.h"
#include "ui/ModelsPane.h"
#include "ui/ThreadListModel.h"
#include "ui/ThreadListPane.h"

namespace qodex::ui {

MainWindow::MainWindow(
    const QString &uniqueName,
    const QString &windowTitle,
    ThreadListModel *threadListModel,
    ApiLogModel *apiLogModel,
    qodex::storage::DatabaseManager *databaseManager,
    LoadedThreadsModel *loadedThreadsModel,
    ModelsModel *modelsModel,
    QWidget *parent
)
    : KDDockWidgets::QtWidgets::MainWindow(
          uniqueName,
          KDDockWidgets::MainWindowOption_HasCentralGroup,
          parent
      ),
      m_windowKey(uniqueName) {
    setWindowTitle(windowTitle);
    resize(1200, 760);

    setDocumentMode(true);

    statusBar()->showMessage(QStringLiteral("Ready"));

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Quit Qodex"), QKeySequence::Quit, this, &MainWindow::quitRequested);

    m_threadMenu = menuBar()->addMenu(QStringLiteral("&Thread"));
    m_viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    if (threadListModel != nullptr) {
        m_threadListPane = new ThreadListPane(threadListModel);
        m_threadListDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ThreadList"));
        m_threadListDock->setTitle(QStringLiteral("Threads"));
        m_threadListDock->setWidget(m_threadListPane);
        addDockWidgetAsTab(m_threadListDock);
    }
    if (apiLogModel != nullptr) {
        m_apiLogPane = new ApiLogPane(apiLogModel);
        m_apiLogDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ApiLog"));
        m_apiLogDock->setTitle(QStringLiteral("API Log"));
        m_apiLogDock->setWidget(m_apiLogPane);
        if (m_threadListDock != nullptr) {
            m_threadListDock->addDockWidgetAsTab(
                m_apiLogDock,
                KDDockWidgets::InitialOption(KDDockWidgets::InitialVisibilityOption::StartHidden)
            );
        } else {
            addDockWidgetAsTab(m_apiLogDock);
            m_apiLogDock->close();
        }
    }
    if (apiLogModel != nullptr && databaseManager != nullptr) {
        m_apiLogInspectorPane = new ApiLogInspectorPane(databaseManager);
        m_apiLogInspectorDock =
            new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ApiLogInspector"));
        m_apiLogInspectorDock->setTitle(QStringLiteral("API Log Inspector"));
        m_apiLogInspectorDock->setWidget(m_apiLogInspectorPane);
        if (m_apiLogDock != nullptr) {
            m_apiLogDock->addDockWidgetAsTab(
                m_apiLogInspectorDock,
                KDDockWidgets::InitialOption(KDDockWidgets::InitialVisibilityOption::StartHidden)
            );
        } else {
            addDockWidgetAsTab(m_apiLogInspectorDock);
            m_apiLogInspectorDock->close();
        }
    }
    if (loadedThreadsModel != nullptr) {
        m_loadedThreadsPane = new LoadedThreadsPane(loadedThreadsModel);
        m_loadedThreadsDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.LoadedThreads"));
        m_loadedThreadsDock->setTitle(QStringLiteral("Loaded Threads"));
        m_loadedThreadsDock->setWidget(m_loadedThreadsPane);
        if (m_threadListDock != nullptr) {
            m_threadListDock->addDockWidgetAsTab(
                m_loadedThreadsDock,
                KDDockWidgets::InitialOption(KDDockWidgets::InitialVisibilityOption::StartVisible)
            );
        } else {
            addDockWidgetAsTab(m_loadedThreadsDock);
        }
    }
    if (modelsModel != nullptr) {
        m_modelsPane = new ModelsPane(modelsModel);
        m_modelsDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.Models"));
        m_modelsDock->setTitle(QStringLiteral("Models"));
        m_modelsDock->setWidget(m_modelsPane);
        if (m_loadedThreadsDock != nullptr) {
            m_loadedThreadsDock->addDockWidgetAsTab(
                m_modelsDock,
                KDDockWidgets::InitialOption(KDDockWidgets::InitialVisibilityOption::StartHidden)
            );
        } else if (m_threadListDock != nullptr) {
            m_threadListDock->addDockWidgetAsTab(
                m_modelsDock,
                KDDockWidgets::InitialOption(KDDockWidgets::InitialVisibilityOption::StartHidden)
            );
        } else {
            addDockWidgetAsTab(m_modelsDock);
            m_modelsDock->close();
        }
    }
    if (m_apiLogPane != nullptr) {
        connect(m_apiLogPane, &ApiLogPane::inspectApiLogRequested, this, &MainWindow::inspectApiLog);
    }

    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
}

MainWindow::~MainWindow() {
    delete m_modelsDock;
    delete m_loadedThreadsDock;
    delete m_apiLogInspectorDock;
    delete m_apiLogDock;
    delete m_threadListDock;
}

ThreadListPane *MainWindow::threadListPane() const {
    return m_threadListPane;
}

ApiLogPane *MainWindow::apiLogPane() const {
    return m_apiLogPane;
}

ApiLogInspectorPane *MainWindow::apiLogInspectorPane() const {
    return m_apiLogInspectorPane;
}

LoadedThreadsPane *MainWindow::loadedThreadsPane() const {
    return m_loadedThreadsPane;
}

ModelsPane *MainWindow::modelsPane() const {
    return m_modelsPane;
}

QString MainWindow::windowKey() const {
    return m_windowKey;
}

QList<QAction *> MainWindow::viewActions() const {
    QList<QAction *> actions;
    if (m_threadListDock != nullptr) {
        actions.append(m_threadListDock->toggleAction());
    }
    if (m_apiLogDock != nullptr) {
        actions.append(m_apiLogDock->toggleAction());
    }
    if (m_apiLogInspectorDock != nullptr) {
        actions.append(m_apiLogInspectorDock->toggleAction());
    }
    if (m_loadedThreadsDock != nullptr) {
        actions.append(m_loadedThreadsDock->toggleAction());
    }
    if (m_modelsDock != nullptr) {
        actions.append(m_modelsDock->toggleAction());
    }
    return actions;
}

void MainWindow::setStatusMessage(const QString &message) {
    statusBar()->showMessage(message);
}

void MainWindow::inspectApiLog(const qint64 apiLogId) {
    if (m_apiLogInspectorPane == nullptr || m_apiLogInspectorDock == nullptr) {
        return;
    }

    m_apiLogInspectorPane->inspectApiLog(apiLogId);
    m_apiLogInspectorDock->show();
    m_apiLogInspectorDock->raise();
}

void MainWindow::rebuildThreadMenu(const QList<ThreadUiMenuEntry> &entries) {
    if (m_threadMenu == nullptr) {
        return;
    }

    m_threadMenu->clear();

    if (entries.isEmpty()) {
        QAction *placeholderAction = m_threadMenu->addAction(QStringLiteral("No Active Thread UI Windows"));
        placeholderAction->setEnabled(false);
        return;
    }

    for (const ThreadUiMenuEntry &entry : entries) {
        QAction *action = m_threadMenu->addAction(entry.title);
        connect(action, &QAction::triggered, this, [this, instanceId = entry.instanceId] {
            emit activateThreadUiRequested(instanceId);
        });
    }
}

void MainWindow::rebuildViewMenu(const QList<QAction *> &actions) {
    if (m_viewMenu == nullptr) {
        return;
    }

    m_viewMenu->clear();
    for (QAction *action : actions) {
        if (action != nullptr) {
            m_viewMenu->addAction(action);
        }
    }
}

void MainWindow::rebuildWindowMenu(const QList<MainWindow *> &windows) {
    if (m_windowMenu == nullptr) {
        return;
    }

    m_windowMenu->clear();
    m_windowMenu->addAction(QStringLiteral("Create &New Window"), this, &MainWindow::createNewWindowRequested);
    m_windowMenu->addSeparator();

    auto *windowGroup = new QActionGroup(m_windowMenu);
    windowGroup->setExclusive(true);

    for (int index = 0; index < windows.size(); ++index) {
        MainWindow *window = windows.at(index);
        if (window == nullptr) {
            continue;
        }

        QAction *action = m_windowMenu->addAction(windowMenuTitleFor(window, index));
        action->setCheckable(true);
        action->setChecked(window == this);
        windowGroup->addAction(action);

        connect(action, &QAction::triggered, this, [window] {
            if (window == nullptr) {
                return;
            }
            window->show();
            window->raise();
            window->activateWindow();
        });
    }
}

QString MainWindow::windowMenuTitleFor(const MainWindow *window, const int index) const {
    const QString title = window != nullptr ? window->windowTitle() : QString{};
    if (!title.isEmpty()) {
        return QStringLiteral("%1 %2").arg(index + 1).arg(title);
    }
    return QStringLiteral("Window %1").arg(index + 1);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    emit aboutToClose(this);
    KDDockWidgets::QtWidgets::MainWindow::closeEvent(event);
}

}  // namespace qodex::ui
