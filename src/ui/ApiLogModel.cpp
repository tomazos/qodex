#include "ui/ApiLogModel.h"

#include <algorithm>

#include <QFont>

namespace qodex::ui {

ApiLogModel::ApiLogModel(qodex::storage::DatabaseManager *databaseManager, QObject *parent)
    : QAbstractTableModel(parent),
      m_databaseManager(databaseManager) {
    Q_ASSERT(m_databaseManager != nullptr);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ApiLogModel::refresh);

    refresh();
}

QModelIndex ApiLogModel::index(const int row, const int column, const QModelIndex &parent) const {
    if (parent.isValid() || row < 0 || column < 0 || column >= ColumnCount || row >= m_rows.size()) {
        return {};
    }

    return createIndex(row, column);
}

QModelIndex ApiLogModel::parent(const QModelIndex &index) const {
    Q_UNUSED(index);
    return {};
}

int ApiLogModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int ApiLogModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant ApiLogModel::data(const QModelIndex &index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || index.column() < 0
        || index.column() >= ColumnCount) {
        return {};
    }

    const qodex::storage::ApiLogListRecord &row = m_rows.at(index.row());
    const Column column = static_cast<Column>(index.column());

    switch (role) {
    case Qt::DisplayRole:
        return displayValueForColumn(row, column);
    case Qt::ToolTipRole:
        if (column == SummaryColumn) {
            return row.summaryText;
        }
        if (column == PayloadColumn) {
            return row.payloadPreview;
        }
        return displayValueForColumn(row, column);
    case Qt::TextAlignmentRole:
        if (column == SuccessColumn || column == LatencyColumn) {
            return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
        }
        return QVariant::fromValue(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
    case Qt::FontRole:
        if (column == TimeColumn || column == LatencyColumn || column == ThreadIdColumn
            || column == JsonRpcIdColumn || column == CorrelationIdColumn || column == SessionIdColumn) {
            return monospaceFont();
        }
        return {};
    default:
        return {};
    }
}

QVariant ApiLogModel::headerData(const int section, const Qt::Orientation orientation, const int role) const {
    if (orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case TimeColumn:
        return QStringLiteral("Time (UTC)");
    case DirectionColumn:
        return QStringLiteral("Direction");
    case KindColumn:
        return QStringLiteral("Kind");
    case MethodColumn:
        return QStringLiteral("Method");
    case SuccessColumn:
        return QStringLiteral("Success");
    case LatencyColumn:
        return QStringLiteral("Latency (ms)");
    case SummaryColumn:
        return QStringLiteral("Summary");
    case ThreadIdColumn:
        return QStringLiteral("Thread Id");
    case JsonRpcIdColumn:
        return QStringLiteral("JSON-RPC Id");
    case CorrelationIdColumn:
        return QStringLiteral("Correlation Id");
    case SessionIdColumn:
        return QStringLiteral("Session");
    case PayloadColumn:
        return QStringLiteral("Payload");
    default:
        return {};
    }
}

Qt::ItemFlags ApiLogModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool ApiLogModel::canFetchMore(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return false;
    }
    return m_rows.size() < m_totalRowCount;
}

void ApiLogModel::fetchMore(const QModelIndex &parent) {
    if (parent.isValid() || !canFetchMore(parent) || m_databaseManager == nullptr) {
        return;
    }

    const int remaining = m_totalRowCount - int(m_rows.size());
    QString errorMessage;
    const QList<qodex::storage::ApiLogListRecord> page = m_databaseManager->loadApiLogPage(
        int(m_rows.size()),
        std::min(kFetchBlockSize, remaining),
        sortFieldForColumn(m_sortColumn),
        m_sortOrder,
        &errorMessage
    );
    if (!errorMessage.isEmpty() || page.isEmpty()) {
        return;
    }

    const int firstRow = m_rows.size();
    const int lastRow = firstRow + page.size() - 1;
    beginInsertRows(QModelIndex{}, firstRow, lastRow);
    for (const qodex::storage::ApiLogListRecord &row : page) {
        m_rows.append(row);
    }
    endInsertRows();
}

void ApiLogModel::sort(const int column, const Qt::SortOrder order) {
    if (column < 0 || column >= ColumnCount) {
        return;
    }

    m_sortColumn = static_cast<Column>(column);
    m_sortOrder = order;
    refresh();
}

void ApiLogModel::refresh() {
    if (m_databaseManager == nullptr || !m_databaseManager->isOpen()) {
        if (m_totalRowCount == 0 && m_rows.isEmpty()) {
            return;
        }

        beginResetModel();
        m_totalRowCount = 0;
        m_rows.clear();
        endResetModel();
        return;
    }

    const int targetRowCount = std::max(kFetchBlockSize, int(m_rows.size()));
    resetRows(targetRowCount);
}

void ApiLogModel::scheduleRefresh() {
    m_refreshTimer.start();
}

QFont ApiLogModel::monospaceFont() {
    static const QFont font = [] {
        QFont builtFont(QStringLiteral("monospace"));
        builtFont.setStyleHint(QFont::TypeWriter);
        builtFont.setFixedPitch(true);
        return builtFont;
    }();
    return font;
}

qodex::storage::ApiLogSortField ApiLogModel::sortFieldForColumn(const Column column) {
    switch (column) {
    case TimeColumn:
        return qodex::storage::ApiLogSortField::TimestampUtc;
    case DirectionColumn:
        return qodex::storage::ApiLogSortField::Direction;
    case KindColumn:
        return qodex::storage::ApiLogSortField::MessageKind;
    case MethodColumn:
        return qodex::storage::ApiLogSortField::Method;
    case SuccessColumn:
        return qodex::storage::ApiLogSortField::Success;
    case LatencyColumn:
        return qodex::storage::ApiLogSortField::LatencyMs;
    case SummaryColumn:
        return qodex::storage::ApiLogSortField::SummaryText;
    case ThreadIdColumn:
        return qodex::storage::ApiLogSortField::ThreadId;
    case JsonRpcIdColumn:
        return qodex::storage::ApiLogSortField::JsonRpcId;
    case CorrelationIdColumn:
        return qodex::storage::ApiLogSortField::CorrelationId;
    case SessionIdColumn:
        return qodex::storage::ApiLogSortField::SessionId;
    case PayloadColumn:
        return qodex::storage::ApiLogSortField::PayloadPreview;
    case ColumnCount:
        break;
    }

    return qodex::storage::ApiLogSortField::TimestampUtc;
}

QString ApiLogModel::displayValueForColumn(const qodex::storage::ApiLogListRecord &row, const Column column) const {
    switch (column) {
    case TimeColumn:
        return row.timestampUtc;
    case DirectionColumn:
        return row.direction;
    case KindColumn:
        return row.messageKind;
    case MethodColumn:
        return row.method;
    case SuccessColumn:
        if (!row.success.has_value()) {
            return {};
        }
        return *row.success ? QStringLiteral("Yes") : QStringLiteral("No");
    case LatencyColumn:
        return row.latencyMs.has_value() ? QString::number(*row.latencyMs) : QString{};
    case SummaryColumn:
        return row.summaryText;
    case ThreadIdColumn:
        return row.threadId;
    case JsonRpcIdColumn:
        return row.jsonrpcId;
    case CorrelationIdColumn:
        return row.correlationId;
    case SessionIdColumn:
        return row.sessionId;
    case PayloadColumn:
        return row.payloadPreview;
    case ColumnCount:
        break;
    }

    return {};
}

void ApiLogModel::resetRows(const int targetRowCount) {
    if (m_databaseManager == nullptr) {
        return;
    }

    QString errorMessage;
    const int totalRowCount = m_databaseManager->apiLogRowCount(&errorMessage);
    if (!errorMessage.isEmpty()) {
        return;
    }

    const int desiredRowCount = std::min(std::max(0, targetRowCount), totalRowCount);
    const QList<qodex::storage::ApiLogListRecord> rows = m_databaseManager->loadApiLogPage(
        0,
        desiredRowCount,
        sortFieldForColumn(m_sortColumn),
        m_sortOrder,
        &errorMessage
    );
    if (!errorMessage.isEmpty()) {
        return;
    }

    beginResetModel();
    m_totalRowCount = totalRowCount;
    m_rows = rows;
    endResetModel();
}

}  // namespace qodex::ui
