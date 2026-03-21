#pragma once

#include <kddockwidgets/MainWindow.h>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

namespace qodex::ui {

class ThreadListModel;
class ThreadListPane;
}

namespace qodex::ui {

class MainWindow final : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        const QString &uniqueName,
        const QString &windowTitle,
        ThreadListModel *threadListModel,
        QWidget *parent = nullptr
    );
    ~MainWindow() override;

    [[nodiscard]] ThreadListPane *threadListPane() const;
    void setStatusMessage(const QString &message);

private:
    KDDockWidgets::QtWidgets::DockWidget *m_threadListDock = nullptr;
    ThreadListPane *m_threadListPane = nullptr;
};

}  // namespace qodex::ui
