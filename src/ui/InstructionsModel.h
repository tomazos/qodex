#pragma once

#include <QAbstractListModel>

namespace qodex::domain {
class InstructionCatalog;
struct InstructionDocumentSummary;
}

namespace qodex::ui {

class InstructionsModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        IsDefaultRole,
        IsRenamableRole,
        IsEditableRole,
        IsDuplicableRole,
    };

    explicit InstructionsModel(QObject *parent = nullptr);

    void setCatalog(qodex::domain::InstructionCatalog *catalog);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

signals:
    void listRebuilt();

private slots:
    void refreshFromCatalog();

private:
    qodex::domain::InstructionCatalog *m_catalog = nullptr;
    QList<qodex::domain::InstructionDocumentSummary> m_entries;
};

}  // namespace qodex::ui
