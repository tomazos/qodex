#include "ui/MainWindow.h"

#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <kddockwidgets/DockWidget.h>

#include "ui/ApiLogModel.h"
#include "ui/ApiLogPane.h"
#include "ui/ThreadListModel.h"
#include "ui/ThreadListPane.h"

namespace qodex::ui {

MainWindow::MainWindow(
    const QString &uniqueName,
    const QString &windowTitle,
    ThreadListModel *threadListModel,
    ApiLogModel *apiLogModel,
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

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    if (threadListModel != nullptr) {
        m_threadListPane = new ThreadListPane(threadListModel);
        m_threadListDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ThreadList"));
        m_threadListDock->setTitle(QStringLiteral("Threads"));
        m_threadListDock->setWidget(m_threadListPane);
        addDockWidgetAsTab(m_threadListDock);
        viewMenu->addAction(m_threadListDock->toggleAction());
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
        viewMenu->addAction(m_apiLogDock->toggleAction());
    }

    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
}

MainWindow::~MainWindow() {
    delete m_apiLogDock;
    delete m_threadListDock;
}

ThreadListPane *MainWindow::threadListPane() const {
    return m_threadListPane;
}

ApiLogPane *MainWindow::apiLogPane() const {
    return m_apiLogPane;
}

QString MainWindow::windowKey() const {
    return m_windowKey;
}

void MainWindow::setStatusMessage(const QString &message) {
    statusBar()->showMessage(message);
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
