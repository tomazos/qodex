#include "ui/ThreadListModel.h"

#include <algorithm>
#include <utility>

#include <QDateTime>
#include <QHash>
#include <QLocale>

#include "domain/ThreadStore.h"

namespace qodex::ui {

ThreadListModel::ThreadListModel(domain::ThreadStore *threadStore, QObject *parent)
    : QAbstractItemModel(parent),
      m_threadStore(threadStore) {
    Q_ASSERT(m_threadStore != nullptr);

    if (m_threadStore != nullptr) {
        rebuildTree();
        connect(
            m_threadStore,
            &domain::ThreadStore::threadListChanged,
            this,
            &ThreadListModel::refreshFromStore
        );
    }
}

QModelIndex ThreadListModel::index(const int row, const int column, const QModelIndex &parent) const {
    if (column < 0 || column >= ColumnCount || row < 0) {
        return {};
    }

    if (!parent.isValid()) {
        if (row >= static_cast<int>(m_rootNodes.size())) {
            return {};
        }
        return createIndex(row, column, m_rootNodes[static_cast<std::size_t>(row)].get());
    }

    const Node *parentNode = nodeFromIndex(parent);
    if (parentNode == nullptr || parentNode->kind == Node::Kind::Thread || parent.column() != ThreadColumn) {
        return {};
    }

    const auto *group = static_cast<const GroupNode *>(parentNode);
    if (row >= static_cast<int>(group->children.size())) {
        return {};
    }

    return createIndex(row, column, group->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ThreadListModel::parent(const QModelIndex &index) const {
    const Node *node = nodeFromIndex(index);
    if (node == nullptr || node->parent == nullptr) {
        return {};
    }

    Node *parentNode = node->parent;
    return createIndex(parentNode->row, ThreadColumn, parentNode);
}

int ThreadListModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid()) {
        return static_cast<int>(m_rootNodes.size());
    }

    if (parent.column() != ThreadColumn) {
        return 0;
    }

    const Node *parentNode = nodeFromIndex(parent);
    if (parentNode == nullptr || parentNode->kind == Node::Kind::Thread) {
        return 0;
    }

    return static_cast<int>(static_cast<const GroupNode *>(parentNode)->children.size());
}

int ThreadListModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid() && parent.column() != ThreadColumn) {
        return 0;
    }
    return ColumnCount;
}

QVariant ThreadListModel::data(const QModelIndex &index, const int role) const {
    const Node *node = nodeFromIndex(index);
    if (node == nullptr || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }

    if (node->kind == Node::Kind::Thread) {
        return threadData(*static_cast<const ThreadNode *>(node), index.column(), role);
    }
    return groupData(*static_cast<const GroupNode *>(node), index.column(), role);
}

QVariant ThreadListModel::headerData(const int section, const Qt::Orientation orientation, const int role) const {
    if (orientation != Qt::Horizontal) {
        return QAbstractItemModel::headerData(section, orientation, role);
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case ThreadColumn:
        return QStringLiteral("Thread");
    case CreatedColumn:
        return QStringLiteral("Created");
    case ModifiedColumn:
        return QStringLiteral("Modified");
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
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
        {StatusRole, "status"},
        {ArchivedRole, "archived"},
    };
}

Qt::ItemFlags ThreadListModel::flags(const QModelIndex &index) const {
    const Node *node = nodeFromIndex(index);
    if (node == nullptr) {
        return Qt::NoItemFlags;
    }
    if (node->kind == Node::Kind::Thread) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    return Qt::ItemIsEnabled;
}

void ThreadListModel::sort(const int column, const Qt::SortOrder order) {
    if (column < 0 || column >= ColumnCount) {
        return;
    }

    m_sortColumn = static_cast<Column>(column);
    m_sortOrder = order;

    emit layoutAboutToBeChanged();
    sortRows();
    emit layoutChanged();
}

QString ThreadListModel::formatTimestamp(const qint64 secondsSinceEpoch) {
    if (secondsSinceEpoch <= 0) {
        return QStringLiteral("—");
    }
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(secondsSinceEpoch), QLocale::ShortFormat);
}

QString ThreadListModel::displayCwd(const QString &cwd) {
    return cwd.trimmed().isEmpty() ? QStringLiteral("(no cwd)") : cwd;
}

const ThreadListModel::Node *ThreadListModel::nodeFromIndex(const QModelIndex &index) {
    return index.isValid() ? static_cast<const Node *>(index.internalPointer()) : nullptr;
}

QVariant ThreadListModel::groupData(const GroupNode &group, const int column, const int role) const {
    switch (role) {
    case Qt::DisplayRole:
        if (column == ThreadColumn) {
            return group.label;
        }
        return {};
    case CwdRole:
        return group.cwd;
    case ArchivedRole:
        return group.kind == Node::Kind::ArchivedRoot || group.kind == Node::Kind::ArchivedCwdGroup;
    case Qt::ToolTipRole:
        return QStringLiteral("%1\n%2 threads").arg(group.label).arg(threadCountUnder(group));
    case Qt::TextAlignmentRole:
        if (column == CreatedColumn || column == ModifiedColumn) {
            return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
        }
        return QVariant::fromValue(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
    default:
        return {};
    }
}

QVariant ThreadListModel::threadData(const ThreadNode &thread, const int column, const int role) const {
    const domain::ThreadSummary &summary = thread.summary;

    switch (role) {
    case Qt::DisplayRole:
        switch (column) {
        case ThreadColumn:
            return summary.title;
        case CreatedColumn:
            return formatTimestamp(summary.createdAt);
        case ModifiedColumn:
            return formatTimestamp(summary.updatedAt);
        default:
            return {};
        }
    case IdRole:
        return summary.id;
    case TitleRole:
        return summary.title;
    case PreviewRole:
        return summary.preview;
    case CwdRole:
        return summary.cwd;
    case CreatedAtRole:
        return summary.createdAt;
    case UpdatedAtRole:
        return summary.updatedAt;
    case StatusRole:
        return summary.statusText;
    case ArchivedRole:
        return summary.archived;
    case Qt::TextAlignmentRole:
        if (column == CreatedColumn || column == ModifiedColumn) {
            return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
        }
        return QVariant::fromValue(Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
    case Qt::ToolTipRole:
        return QStringLiteral("%1\nCreated: %2\nModified: %3\n%4\n%5").arg(
            summary.title,
            formatTimestamp(summary.createdAt),
            formatTimestamp(summary.updatedAt),
            summary.cwd,
            summary.statusText
        );
    default:
        return {};
    }
}

int ThreadListModel::threadCountUnder(const GroupNode &group) {
    int count = 0;
    for (const std::unique_ptr<Node> &child : group.children) {
        if (child->kind == Node::Kind::Thread) {
            ++count;
        } else {
            count += threadCountUnder(*static_cast<const GroupNode *>(child.get()));
        }
    }
    return count;
}

void ThreadListModel::assignRows(GroupNode &group) {
    for (std::size_t index = 0; index < group.children.size(); ++index) {
        group.children[index]->row = static_cast<int>(index);
        if (group.children[index]->kind != Node::Kind::Thread) {
            assignRows(*static_cast<GroupNode *>(group.children[index].get()));
        }
    }
}

void ThreadListModel::rebuildTree() {
    m_rootNodes.clear();
    if (m_threadStore == nullptr) {
        return;
    }

    QHash<QString, GroupNode *> activeGroupsByCwd;
    auto archivedRoot = std::make_unique<GroupNode>(Node::Kind::ArchivedRoot);
    archivedRoot->label = QStringLiteral("Archived");
    QHash<QString, GroupNode *> archivedGroupsByCwd;

    const QList<domain::ThreadSummary> summaries = m_threadStore->threadSummaries();
    m_rootNodes.reserve(static_cast<std::size_t>(summaries.size()) + 1);

    for (const domain::ThreadSummary &summary : summaries) {
        GroupNode *cwdGroup = nullptr;
        if (summary.archived) {
            cwdGroup = archivedGroupsByCwd.value(summary.cwd, nullptr);
            if (cwdGroup == nullptr) {
                auto ownedGroup = std::make_unique<GroupNode>(Node::Kind::ArchivedCwdGroup, archivedRoot.get());
                ownedGroup->cwd = summary.cwd;
                ownedGroup->label = displayCwd(summary.cwd);
                cwdGroup = ownedGroup.get();
                archivedGroupsByCwd.insert(summary.cwd, cwdGroup);
                archivedRoot->children.push_back(std::move(ownedGroup));
            }
        } else {
            cwdGroup = activeGroupsByCwd.value(summary.cwd, nullptr);
            if (cwdGroup == nullptr) {
                auto ownedGroup = std::make_unique<GroupNode>(Node::Kind::ActiveCwdGroup);
                ownedGroup->cwd = summary.cwd;
                ownedGroup->label = displayCwd(summary.cwd);
                cwdGroup = ownedGroup.get();
                activeGroupsByCwd.insert(summary.cwd, cwdGroup);
                m_rootNodes.push_back(std::move(ownedGroup));
            }
        }

        auto threadNode = std::make_unique<ThreadNode>(cwdGroup);
        threadNode->summary = summary;
        cwdGroup->children.push_back(std::move(threadNode));
    }

    m_rootNodes.push_back(std::move(archivedRoot));
    sortRows();
}

void ThreadListModel::sortRows() {
    if (m_rootNodes.empty()) {
        return;
    }

    auto rootComparator = [](const std::unique_ptr<Node> &left, const std::unique_ptr<Node> &right) {
        if (left->kind == Node::Kind::ArchivedRoot) {
            return false;
        }
        if (right->kind == Node::Kind::ArchivedRoot) {
            return true;
        }

        const auto *leftGroup = static_cast<const GroupNode *>(left.get());
        const auto *rightGroup = static_cast<const GroupNode *>(right.get());
        const int byLabel = QString::localeAwareCompare(leftGroup->label, rightGroup->label);
        if (byLabel != 0) {
            return byLabel < 0;
        }
        return leftGroup->cwd < rightGroup->cwd;
    };

    std::sort(m_rootNodes.begin(), m_rootNodes.end(), rootComparator);

    const auto threadComparator = [this](const std::unique_ptr<Node> &left, const std::unique_ptr<Node> &right) {
        const auto *leftThread = static_cast<const ThreadNode *>(left.get());
        const auto *rightThread = static_cast<const ThreadNode *>(right.get());
        const auto compare = [this](const int value) {
            return m_sortOrder == Qt::AscendingOrder ? value : -value;
        };

        switch (m_sortColumn) {
        case ThreadColumn: {
            const int byTitle = compare(QString::localeAwareCompare(leftThread->summary.title, rightThread->summary.title));
            if (byTitle != 0) {
                return byTitle < 0;
            }
            break;
        }
        case CreatedColumn:
            if (leftThread->summary.createdAt != rightThread->summary.createdAt) {
                return m_sortOrder == Qt::AscendingOrder ? leftThread->summary.createdAt < rightThread->summary.createdAt
                                                         : leftThread->summary.createdAt > rightThread->summary.createdAt;
            }
            break;
        case ModifiedColumn:
            if (leftThread->summary.updatedAt != rightThread->summary.updatedAt) {
                return m_sortOrder == Qt::AscendingOrder ? leftThread->summary.updatedAt < rightThread->summary.updatedAt
                                                         : leftThread->summary.updatedAt > rightThread->summary.updatedAt;
            }
            break;
        default:
            break;
        }

        return compare(QString::localeAwareCompare(leftThread->summary.id, rightThread->summary.id)) < 0;
    };

    for (std::size_t rootIndex = 0; rootIndex < m_rootNodes.size(); ++rootIndex) {
        m_rootNodes[rootIndex]->row = static_cast<int>(rootIndex);
        if (m_rootNodes[rootIndex]->kind == Node::Kind::Thread) {
            continue;
        }

        auto *group = static_cast<GroupNode *>(m_rootNodes[rootIndex].get());
        if (group->kind == Node::Kind::ArchivedRoot) {
            std::sort(
                group->children.begin(),
                group->children.end(),
                [](const std::unique_ptr<Node> &left, const std::unique_ptr<Node> &right) {
                    const auto *leftGroup = static_cast<const GroupNode *>(left.get());
                    const auto *rightGroup = static_cast<const GroupNode *>(right.get());
                    const int byLabel = QString::localeAwareCompare(leftGroup->label, rightGroup->label);
                    if (byLabel != 0) {
                        return byLabel < 0;
                    }
                    return leftGroup->cwd < rightGroup->cwd;
                }
            );

            for (const std::unique_ptr<Node> &archivedGroupNode : group->children) {
                auto *archivedGroup = static_cast<GroupNode *>(archivedGroupNode.get());
                std::sort(archivedGroup->children.begin(), archivedGroup->children.end(), threadComparator);
                assignRows(*archivedGroup);
            }
            assignRows(*group);
        } else {
            std::sort(group->children.begin(), group->children.end(), threadComparator);
            assignRows(*group);
        }
    }
}

void ThreadListModel::refreshFromStore() {
    beginResetModel();
    rebuildTree();
    endResetModel();
}

}  // namespace qodex::ui
