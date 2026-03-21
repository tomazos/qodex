#pragma once

#include <QAbstractListModel>
#include <QList>

#include "domain/CodexTypes.h"

namespace qodex::domain {
class ThreadStore;
}

namespace qodex::ui {

class ThreadListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        CwdRole,
        UpdatedAtRole,
        StatusRole,
    };

    explicit ThreadListModel(domain::ThreadStore *threadStore, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    void refreshFromStore();

    domain::ThreadStore *m_threadStore = nullptr;
    QList<domain::ThreadSummary> m_rows;
};

}  // namespace qodex::ui
