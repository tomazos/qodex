#pragma once

#include <QCloseEvent>
#include <QList>
#include <QString>
#include <kddockwidgets/MainWindow.h>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

class QAction;
class QMenu;

namespace qodex::ui {

class ApiLogModel;
class ApiLogPane;
class ApiLogInspectorPane;
class InstructionEditorPane;
class InstructionsModel;
class InstructionsPane;
class LoadedThreadsModel;
class LoadedThreadsPane;
class ModelsModel;
class ModelsPane;
class ThreadListModel;
class ThreadListPane;
class MainWindow;
}

namespace qodex::storage {
class DatabaseManager;
}

namespace qodex::domain {
class InstructionCatalog;
}

namespace qodex::ui {

struct ThreadUiMenuEntry {
    int instanceId = 0;
    QString title;
};

class MainWindow final : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        const QString &uniqueName,
        const QString &windowTitle,
        ThreadListModel *threadListModel = nullptr,
        ApiLogModel *apiLogModel = nullptr,
        qodex::storage::DatabaseManager *databaseManager = nullptr,
        LoadedThreadsModel *loadedThreadsModel = nullptr,
        qodex::domain::InstructionCatalog *instructionCatalog = nullptr,
        InstructionsModel *instructionsModel = nullptr,
        ModelsModel *modelsModel = nullptr,
        QWidget *parent = nullptr
    );
    ~MainWindow() override;

    [[nodiscard]] ThreadListPane *threadListPane() const;
    [[nodiscard]] ApiLogPane *apiLogPane() const;
    [[nodiscard]] ApiLogInspectorPane *apiLogInspectorPane() const;
    [[nodiscard]] InstructionsPane *instructionsPane() const;
    [[nodiscard]] InstructionEditorPane *instructionEditorPane() const;
    [[nodiscard]] LoadedThreadsPane *loadedThreadsPane() const;
    [[nodiscard]] ModelsPane *modelsPane() const;
    [[nodiscard]] QString windowKey() const;
    [[nodiscard]] QList<QAction *> viewActions() const;
    void setStatusMessage(const QString &message);
    void rebuildThreadMenu(const QList<ThreadUiMenuEntry> &entries);
    void rebuildViewMenu(const QList<QAction *> &actions);
    void rebuildWindowMenu(const QList<MainWindow *> &windows);

signals:
    void activateThreadUiRequested(int instanceId);
    void createNewWindowRequested();
    void quitRequested();
    void aboutToClose(qodex::ui::MainWindow *window);

private:
    void inspectApiLog(qint64 apiLogId);
    void viewInstruction(const QString &instructionKey);
    void editInstruction(const QString &instructionKey);
    QString windowMenuTitleFor(const MainWindow *window, int index) const;
    void closeEvent(QCloseEvent *event) override;

    QString m_windowKey;
    KDDockWidgets::QtWidgets::DockWidget *m_threadListDock = nullptr;
    ThreadListPane *m_threadListPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_apiLogDock = nullptr;
    ApiLogPane *m_apiLogPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_apiLogInspectorDock = nullptr;
    ApiLogInspectorPane *m_apiLogInspectorPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_instructionsDock = nullptr;
    InstructionsPane *m_instructionsPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_instructionEditorDock = nullptr;
    InstructionEditorPane *m_instructionEditorPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_loadedThreadsDock = nullptr;
    LoadedThreadsPane *m_loadedThreadsPane = nullptr;
    KDDockWidgets::QtWidgets::DockWidget *m_modelsDock = nullptr;
    ModelsPane *m_modelsPane = nullptr;
    QMenu *m_threadMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_windowMenu = nullptr;
};

}  // namespace qodex::ui
