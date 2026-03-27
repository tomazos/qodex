#include "ui/InstructionsPane.h"

#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include "domain/InstructionCatalog.h"
#include "ui/InstructionsModel.h"

namespace qodex::ui {

namespace {

QString instructionKeyForIndex(const QModelIndex &index) {
    return index.data(InstructionsModel::KeyRole).toString();
}

}  // namespace

InstructionsPane::InstructionsPane(
    InstructionsModel *model,
    qodex::domain::InstructionCatalog *catalog,
    QWidget *parent
)
    : QWidget(parent),
      m_model(model),
      m_catalog(catalog) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_listView = new QListView(this);
    m_listView->setModel(m_model);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listView->setAlternatingRowColors(true);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_listView, 1);

    connect(m_listView, &QWidget::customContextMenuRequested, this, &InstructionsPane::showContextMenu);
    connect(m_listView, &QListView::doubleClicked, this, &InstructionsPane::openViewForIndex);
}

void InstructionsPane::showContextMenu(const QPoint &position) {
    if (m_listView == nullptr || m_model == nullptr || m_catalog == nullptr) {
        return;
    }

    const QModelIndex index = m_listView->indexAt(position);
    if (!index.isValid()) {
        return;
    }

    if (m_listView->selectionModel() != nullptr) {
        m_listView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    }

    const QString instructionKey = instructionKeyForIndex(index);
    const bool isRenamable = index.data(InstructionsModel::IsRenamableRole).toBool();
    const bool isEditable = index.data(InstructionsModel::IsEditableRole).toBool();
    const bool isDuplicable = index.data(InstructionsModel::IsDuplicableRole).toBool();

    QMenu menu(this);
    QAction *renameAction = menu.addAction(QStringLiteral("Rename"));
    renameAction->setEnabled(isRenamable);
    QAction *viewAction = menu.addAction(QStringLiteral("View"));
    QAction *editAction = menu.addAction(QStringLiteral("Edit"));
    editAction->setEnabled(isEditable);
    QAction *duplicateAction = menu.addAction(QStringLiteral("Duplicate"));
    duplicateAction->setEnabled(isDuplicable);

    const QAction *selectedAction = menu.exec(m_listView->viewport()->mapToGlobal(position));
    if (selectedAction == nullptr) {
        return;
    }

    if (selectedAction == viewAction) {
        emit viewInstructionRequested(instructionKey);
        return;
    }

    if (selectedAction == editAction) {
        emit editInstructionRequested(instructionKey);
        return;
    }

    if (selectedAction == renameAction) {
        const QString currentName = index.data(Qt::DisplayRole).toString();
        bool accepted = false;
        const QString newName = QInputDialog::getText(
            this,
            QStringLiteral("Rename Instruction"),
            QStringLiteral("Name:"),
            QLineEdit::Normal,
            currentName,
            &accepted
        );
        if (!accepted) {
            return;
        }

        QString errorMessage;
        if (!m_catalog->renameInstruction(instructionKey, newName, &errorMessage)) {
            QMessageBox::warning(
                this,
                QStringLiteral("Rename Instruction"),
                errorMessage.isEmpty() ? QStringLiteral("Failed to rename instruction.") : errorMessage
            );
            return;
        }
        emit instructionRenamed(instructionKey);
        return;
    }

    if (selectedAction == duplicateAction) {
        QString errorMessage;
        const auto duplicated = m_catalog->duplicateInstruction(instructionKey, &errorMessage);
        if (!duplicated.has_value()) {
            QMessageBox::warning(
                this,
                QStringLiteral("Duplicate Instruction"),
                errorMessage.isEmpty() ? QStringLiteral("Failed to duplicate instruction.") : errorMessage
            );
            return;
        }
        emit editInstructionRequested(duplicated->key);
    }
}

void InstructionsPane::openViewForIndex(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }

    const QString instructionKey = instructionKeyForIndex(index);
    if (!instructionKey.isEmpty()) {
        emit viewInstructionRequested(instructionKey);
    }
}

}  // namespace qodex::ui
