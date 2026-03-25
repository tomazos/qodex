#pragma once

#include <QStandardItemModel>

namespace qodex::app {
class SessionController;
}

namespace qodex::ui {

class ModelsModel final : public QStandardItemModel {
    Q_OBJECT

public:
    explicit ModelsModel(QObject *parent = nullptr);

    void setSessionController(qodex::app::SessionController *sessionController);

signals:
    void treeRebuilt();

private slots:
    void refreshFromController();

private:
    qodex::app::SessionController *m_sessionController = nullptr;
};

}  // namespace qodex::ui
