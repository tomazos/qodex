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
#include "ui/ThreadTranscriptPane.h"

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

    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
}

MainWindow::~MainWindow() {
    for (auto it = m_threadTranscriptDocks.begin(); it != m_threadTranscriptDocks.end(); ++it) {
        if (it.value() != nullptr) {
            delete it.value();
        }
    }
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

QList<QAction *> MainWindow::viewActions() const {
    QList<QAction *> actions;
    if (m_threadListDock != nullptr) {
        actions.append(m_threadListDock->toggleAction());
    }
    if (m_apiLogDock != nullptr) {
        actions.append(m_apiLogDock->toggleAction());
    }
    return actions;
}

void MainWindow::setStatusMessage(const QString &message) {
    statusBar()->showMessage(message);
}

void MainWindow::showThreadTranscript(const QString &threadId, const QString &title, const QString &transcript) {
    if (threadId.isEmpty()) {
        return;
    }

    KDDockWidgets::QtWidgets::DockWidget *dock = m_threadTranscriptDocks.value(threadId, nullptr);
    ThreadTranscriptPane *pane = m_threadTranscriptPanes.value(threadId, nullptr);
    if (dock == nullptr || pane == nullptr) {
        pane = new ThreadTranscriptPane;
        dock = new KDDockWidgets::QtWidgets::DockWidget(
            QStringLiteral("qodex.ThreadTranscript.%1").arg(threadId),
            KDDockWidgets::DockWidgetOption_DeleteOnClose,
            KDDockWidgets::LayoutSaverOption::Skip
        );
        dock->setWidget(pane);
        m_threadTranscriptDocks.insert(threadId, dock);
        m_threadTranscriptPanes.insert(threadId, pane);

        connect(dock, &QObject::destroyed, this, [this, threadId] {
            m_threadTranscriptDocks.remove(threadId);
            m_threadTranscriptPanes.remove(threadId);
        });

        if (m_threadListDock != nullptr) {
            m_threadListDock->addDockWidgetAsTab(dock);
        } else if (m_apiLogDock != nullptr) {
            m_apiLogDock->addDockWidgetAsTab(dock);
        } else {
            addDockWidgetAsTab(dock);
        }
    }

    dock->setTitle(title);
    pane->setTranscriptText(transcript);
    dock->show();
    dock->raise();
}

void MainWindow::closeThreadTranscript(const QString &threadId) {
    if (KDDockWidgets::QtWidgets::DockWidget *dock = m_threadTranscriptDocks.value(threadId, nullptr)) {
        dock->close();
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
