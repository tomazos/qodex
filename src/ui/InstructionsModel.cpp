#include "ui/InstructionsModel.h"

#include <QFont>

#include "domain/InstructionCatalog.h"

namespace qodex::ui {

InstructionsModel::InstructionsModel(QObject *parent)
    : QAbstractListModel(parent) {}

void InstructionsModel::setCatalog(qodex::domain::InstructionCatalog *catalog) {
    if (m_catalog == catalog) {
        return;
    }

    if (m_catalog != nullptr) {
        disconnect(m_catalog, nullptr, this, nullptr);
    }

    m_catalog = catalog;
    if (m_catalog != nullptr) {
        connect(m_catalog, &qodex::domain::InstructionCatalog::instructionsChanged, this, &InstructionsModel::refreshFromCatalog);
    }

    refreshFromCatalog();
}

int InstructionsModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant InstructionsModel::data(const QModelIndex &index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const qodex::domain::InstructionDocumentSummary &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return entry.name;
    case Qt::ToolTipRole:
        return entry.isDefault ? QStringLiteral("Bundled immutable Codex default instructions.")
                               : QStringLiteral("User instruction document.");
    case Qt::FontRole: {
        QFont font;
        font.setBold(entry.isDefault);
        return font;
    }
    case KeyRole:
        return entry.key;
    case IsDefaultRole:
        return entry.isDefault;
    case IsRenamableRole:
        return entry.isRenamable;
    case IsEditableRole:
        return entry.isEditable;
    case IsDuplicableRole:
        return entry.isDuplicable;
    default:
        return {};
    }
}

void InstructionsModel::refreshFromCatalog() {
    beginResetModel();
    m_entries.clear();
    if (m_catalog != nullptr) {
        QString ignoredError;
        m_entries = m_catalog->instructionSummaries(&ignoredError);
    }
    endResetModel();
    emit listRebuilt();
}

}  // namespace qodex::ui
