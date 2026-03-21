#include "ui/MainWindow.h"

#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <kddockwidgets/DockWidget.h>

#include "ui/ThreadListModel.h"
#include "ui/ThreadListPane.h"

namespace qodex::ui {

MainWindow::MainWindow(
    const QString &uniqueName,
    const QString &windowTitle,
    ThreadListModel *threadListModel,
    const bool createThreadListDock,
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

    auto *detailPane = new QWidget;
    auto *detailLayout = new QVBoxLayout(detailPane);
    auto *detailHeadline = new QLabel(QStringLiteral("Thread Summary"), detailPane);
    detailHeadline->setObjectName(QStringLiteral("detailHeadline"));

    m_threadSummaryLabel = new QLabel(
        createThreadListDock
            ? QStringLiteral("Starting qodex…")
            : QStringLiteral("Secondary window.\n\nDrag the Threads pane here to test cross-window docking."),
        detailPane
    );
    m_threadSummaryLabel->setWordWrap(true);
    m_threadSummaryLabel->setObjectName(QStringLiteral("threadSummary"));

    detailLayout->addWidget(detailHeadline);
    detailLayout->addWidget(m_threadSummaryLabel, 1);
    detailLayout->setContentsMargins(16, 16, 16, 16);
    detailPane->setLayout(detailLayout);

    m_summaryDock = new KDDockWidgets::QtWidgets::DockWidget(uniqueName + QStringLiteral(".Summary"));
    m_summaryDock->setTitle(QStringLiteral("Summary"));
    m_summaryDock->setWidget(detailPane);
    addDockWidget(m_summaryDock, KDDockWidgets::Location_OnTop);

    statusBar()->showMessage(QStringLiteral("Ready"));

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_summaryDock->toggleAction());

    if (createThreadListDock) {
        m_threadListPane = new ThreadListPane(threadListModel);
        m_threadListDock = new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("qodex.ThreadList"));
        m_threadListDock->setTitle(QStringLiteral("Threads"));
        m_threadListDock->setWidget(m_threadListPane);
        addDockWidget(m_threadListDock, KDDockWidgets::Location_OnLeft, m_summaryDock);
        viewMenu->addAction(m_threadListDock->toggleAction());
    }
}

MainWindow::~MainWindow() {
    delete m_threadListDock;
    delete m_summaryDock;
}

ThreadListPane *MainWindow::threadListPane() const {
    return m_threadListPane;
}

void MainWindow::setStatusMessage(const QString &message) {
    statusBar()->showMessage(message);
}

void MainWindow::setThreadSummaryText(const QString &message) {
    m_threadSummaryLabel->setText(message);
}

}  // namespace qodex::ui
