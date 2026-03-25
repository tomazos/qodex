#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QTimer>

#include "storage/DatabaseManager.h"

namespace qodex::ui {

class ApiLogModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Role {
        ApiLogIdRole = Qt::UserRole + 1,
    };

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
    void ensureRowsCached(int firstRow, int lastRow);

public slots:
    void refresh();
    void scheduleRefresh();

private:
    [[nodiscard]] static QFont monospaceFont();
    [[nodiscard]] static qodex::storage::ApiLogSortField sortFieldForColumn(Column column);
    [[nodiscard]] QString displayValueForColumn(const qodex::storage::ApiLogListRecord &row, Column column) const;
    void reloadCacheWindow(int firstRow, int lastRow);
    void replaceCacheWindow(int newOffset, QList<qodex::storage::ApiLogListRecord> rows);

    static constexpr int kCacheWindowSize = 800;
    static constexpr int kCacheRetainMargin = 160;

    qodex::storage::DatabaseManager *m_databaseManager = nullptr;
    QList<qodex::storage::ApiLogListRecord> m_rows;
    int m_cacheOffset = 0;
    int m_totalRowCount = 0;
    int m_preferredFirstRow = 0;
    int m_preferredLastRow = 39;
    Column m_sortColumn = TimeColumn;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
    QTimer m_refreshTimer;
};

}  // namespace qodex::ui
