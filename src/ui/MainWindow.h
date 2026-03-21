#pragma once

#include <kddockwidgets/MainWindow.h>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

namespace qodex::ui {

class ThreadListModel;
class ThreadListPane;
}

class QLabel;

namespace qodex::ui {

class MainWindow final : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        const QString &uniqueName,
        const QString &windowTitle,
        ThreadListModel *threadListModel,
        bool createThreadListDock,
        QWidget *parent = nullptr
    );
    ~MainWindow() override;

    [[nodiscard]] ThreadListPane *threadListPane() const;
    void setStatusMessage(const QString &message);
    void setThreadSummaryText(const QString &message);

private:
    KDDockWidgets::QtWidgets::DockWidget *m_summaryDock = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_threadListDock = nullptr;
    ThreadListPane *m_threadListPane = nullptr;
    QLabel *m_threadSummaryLabel = nullptr;
};

}  // namespace qodex::ui
