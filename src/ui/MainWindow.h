#pragma once

#include <QList>
#include <kddockwidgets/MainWindow.h>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

namespace qodex::ui {

class ThreadListModel;
class ThreadListPane;
class MainWindow;
}

namespace qodex::ui {

class MainWindow final : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        const QString &uniqueName,
        const QString &windowTitle,
        ThreadListModel *threadListModel = nullptr,
        QWidget *parent = nullptr
    );
    ~MainWindow() override;

    [[nodiscard]] ThreadListPane *threadListPane() const;
    void setStatusMessage(const QString &message);
    void rebuildWindowMenu(const QList<MainWindow *> &windows);

signals:
    void createNewWindowRequested();

private:
    QString windowMenuTitleFor(const MainWindow *window, int index) const;

    KDDockWidgets::QtWidgets::DockWidget *m_threadListDock = nullptr;
    ThreadListPane *m_threadListPane = nullptr;
    QMenu *m_windowMenu = nullptr;
};

}  // namespace qodex::ui
