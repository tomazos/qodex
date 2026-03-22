#pragma once

#include <QAbstractItemModel>
#include <QList>
#include <memory>
#include <vector>

#include "domain/CodexTypes.h"

namespace qodex::domain {
class ThreadStore;
}

namespace qodex::ui {

class ThreadListModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ThreadColumn = 0,
        CreatedColumn,
        ModifiedColumn,
        StatusColumn,
        SourceColumn,
        ModelProviderColumn,
        PreviewColumn,
        CwdColumn,
        IdColumn,
        CliVersionColumn,
        PathColumn,
        EphemeralColumn,
        AgentNicknameColumn,
        AgentRoleColumn,
        GitOriginColumn,
        GitBranchColumn,
        GitShaColumn,
        ColumnCount,
    };

    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        CwdRole,
        CreatedAtRole,
        UpdatedAtRole,
        StatusRole,
        ArchivedRole,
    };

    explicit ThreadListModel(domain::ThreadStore *threadStore, QObject *parent = nullptr);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    struct Node {
        enum class Kind {
            ActiveCwdGroup,
            ArchivedRoot,
            ArchivedCwdGroup,
            Thread,
        };

        explicit Node(Kind nodeKind, Node *parentNode = nullptr)
            : kind(nodeKind),
              parent(parentNode) {
        }

        virtual ~Node() = default;

        Kind kind;
        Node *parent = nullptr;
        int row = 0;
    };

    struct ThreadNode;

    struct GroupNode final : Node {
        explicit GroupNode(Node::Kind kind, Node *parentNode = nullptr)
            : Node(kind, parentNode) {
        }

        QString label;
        QString cwd;
        std::vector<std::unique_ptr<Node>> children;
    };

    struct ThreadNode final : Node {
        explicit ThreadNode(Node *parentNode)
            : Node(Node::Kind::Thread, parentNode) {
        }

        domain::ThreadSummary summary;
    };

    [[nodiscard]] static QString formatTimestamp(qint64 secondsSinceEpoch);
    [[nodiscard]] static QString displayCwd(const QString &cwd);
    [[nodiscard]] static const Node *nodeFromIndex(const QModelIndex &index);
    [[nodiscard]] QVariant groupData(const GroupNode &group, int column, int role) const;
    [[nodiscard]] QVariant threadData(const ThreadNode &thread, int column, int role) const;
    [[nodiscard]] static int threadCountUnder(const GroupNode &group);
    void assignRows(GroupNode &group);
    void rebuildTree();
    void sortRows();
    void refreshFromStore();

    domain::ThreadStore *m_threadStore = nullptr;
    std::vector<std::unique_ptr<Node>> m_rootNodes;
    Column m_sortColumn = ModifiedColumn;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
};

}  // namespace qodex::ui
