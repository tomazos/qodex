#pragma once

#include <QList>
#include <QCloseEvent>
#include <kddockwidgets/MainWindow.h>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

namespace qodex::ui {

class ApiLogModel;
class ApiLogPane;
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
        ApiLogModel *apiLogModel = nullptr,
        QWidget *parent = nullptr
    );
    ~MainWindow() override;

    [[nodiscard]] ThreadListPane *threadListPane() const;
    [[nodiscard]] ApiLogPane *apiLogPane() const;
    [[nodiscard]] QString windowKey() const;
    void setStatusMessage(const QString &message);
    void rebuildWindowMenu(const QList<MainWindow *> &windows);

signals:
    void createNewWindowRequested();
    void quitRequested();
    void aboutToClose(qodex::ui::MainWindow *window);

private:
    QString windowMenuTitleFor(const MainWindow *window, int index) const;
    void closeEvent(QCloseEvent *event) override;

    QString m_windowKey;
    KDDockWidgets::QtWidgets::DockWidget *m_threadListDock = nullptr;
    ThreadListPane *m_threadListPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_apiLogDock = nullptr;
    ApiLogPane *m_apiLogPane = nullptr;
    QMenu *m_windowMenu = nullptr;
};

}  // namespace qodex::ui
