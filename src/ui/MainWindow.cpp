#include "ui/MainWindow.h"

#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <kddockwidgets/DockWidget.h>

#include "ui/ThreadListModel.h"
#include "ui/ThreadListPane.h"

namespace qodex::ui {

MainWindow::MainWindow(
    const QString &uniqueName,
    const QString &windowTitle,
    ThreadListModel *threadListModel,
    QWidget *parent
)
    : KDDockWidgets::QtWidgets::MainWindow(
          uniqueName,
          KDDockWidgets::MainWindowOption_HasCentralGroup,
          parent
      ) {
    setWindowTitle(windowTitle);
    resize(1200, 760);

    setDocumentMode(true);

    statusBar()->showMessage(QStringLiteral("Ready"));

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    m_threadListPane = new ThreadListPane(threadListModel);
    m_threadListDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ThreadList"));
    m_threadListDock->setTitle(QStringLiteral("Threads"));
    m_threadListDock->setWidget(m_threadListPane);
    addDockWidget(m_threadListDock, KDDockWidgets::Location_OnLeft);
    viewMenu->addAction(m_threadListDock->toggleAction());
}

MainWindow::~MainWindow() {
    delete m_threadListDock;
}

ThreadListPane *MainWindow::threadListPane() const {
    return m_threadListPane;
}

void MainWindow::setStatusMessage(const QString &message) {
    statusBar()->showMessage(message);
}

}  // namespace qodex::ui
