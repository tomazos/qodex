#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QTimer>

#include "storage/DatabaseManager.h"

namespace qodex::ui {

class ApiLogModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        TimeColumn = 0,
        DirectionColumn,
        KindColumn,
        MethodColumn,
        SuccessColumn,
        LatencyColumn,
        SummaryColumn,
        ThreadIdColumn,
        JsonRpcIdColumn,
        CorrelationIdColumn,
        SessionIdColumn,
        PayloadColumn,
        ColumnCount,
    };

    explicit ApiLogModel(qodex::storage::DatabaseManager *databaseManager, QObject *parent = nullptr);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

public slots:
    void refresh();
    void scheduleRefresh();

private:
    [[nodiscard]] static QFont monospaceFont();
    [[nodiscard]] static qodex::storage::ApiLogSortField sortFieldForColumn(Column column);
    [[nodiscard]] QString displayValueForColumn(const qodex::storage::ApiLogListRecord &row, Column column) const;
    void resetRows(int targetRowCount);

    static constexpr int kFetchBlockSize = 250;

    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    QList<qodex::storage::ApiLogListRecord> m_rows;
    int m_totalRowCount = 0;
    Column m_sortColumn = TimeColumn;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
    QTimer m_refreshTimer;
};

}  // namespace qodex::ui
