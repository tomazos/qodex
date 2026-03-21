#include "ui/ThreadListModel.h"

#include <QDateTime>

#include "domain/ThreadStore.h"

namespace qodex::ui {

ThreadListModel::ThreadListModel(domain::ThreadStore *threadStore, QObject *parent)
    : QAbstractListModel(parent),
      m_threadStore(threadStore) {
    Q_ASSERT(m_threadStore != nullptr);

    if (m_threadStore != nullptr) {
        m_rows = m_threadStore->threadSummaries();
        connect(
            m_threadStore,
            &domain::ThreadStore::threadListChanged,
            this,
            &ThreadListModel::refreshFromStore
        );
    }
}

int ThreadListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant ThreadListModel::data(const QModelIndex &index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const domain::ThreadSummary &summary = m_rows.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return summary.title;
    case IdRole:
        return summary.id;
    case PreviewRole:
        return summary.preview;
    case CwdRole:
        return summary.cwd;
    case UpdatedAtRole:
        return summary.updatedAt;
    case StatusRole:
        return summary.statusText;
    case Qt::ToolTipRole:
        return QStringLiteral("%1\n%2\n%3")
            .arg(summary.preview, summary.cwd, summary.statusText);
    default:
        return {};
    }
}

QHash<int, QByteArray> ThreadListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {TitleRole, "title"},
        {PreviewRole, "preview"},
        {CwdRole, "cwd"},
        {UpdatedAtRole, "updatedAt"},
        {StatusRole, "status"},
    };
}

void ThreadListModel::refreshFromStore() {
    beginResetModel();
    m_rows = m_threadStore != nullptr ? m_threadStore->threadSummaries() : QList<domain::ThreadSummary>{};
    endResetModel();
}

}  // namespace qodex::ui
