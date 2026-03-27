#pragma once

#include <QModelIndex>
#include <QWidget>

class QListView;

namespace qodex::domain {
class InstructionCatalog;
}

namespace qodex::ui {

class InstructionsModel;

class InstructionsPane final : public QWidget {
    Q_OBJECT

public:
    explicit InstructionsPane(
        InstructionsModel *model,
        qodex::domain::InstructionCatalog *catalog,
        QWidget *parent = nullptr
    );

signals:
    void viewInstructionRequested(const QString &instructionKey);
    void editInstructionRequested(const QString &instructionKey);
    void instructionRenamed(const QString &instructionKey);

private:
    void showContextMenu(const QPoint &position);
    void openViewForIndex(const QModelIndex &index);

    InstructionsModel *m_model = nullptr;
    qodex::domain::InstructionCatalog *m_catalog = nullptr;
    QListView *m_listView = nullptr;
};

}  // namespace qodex::ui
