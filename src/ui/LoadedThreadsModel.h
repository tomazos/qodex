#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QMetaObject>

#include <memory>

namespace qodex::app {
class LoadedThread;
class SessionController;
}

namespace qodex::domain::threadmodel {
class AbstractItem;
class Turn;
}

namespace qodex::ui {

class LoadedThreadsModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit LoadedThreadsModel(QObject *parent = nullptr);
    ~LoadedThreadsModel() override;

    void setSessionController(qodex::app::SessionController *sessionController);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    enum class NodeKind {
        Root,
        Thread,
        Turn,
        Item,
        Property,
    };

    struct Node;

    struct LoadedThreadConnections {
        QMetaObject::Connection destroyed;
        QMetaObject::Connection snapshotRebuilt;
        QMetaObject::Connection threadPresentationChanged;
        QMetaObject::Connection turnInserted;
        QMetaObject::Connection turnChanged;
        QMetaObject::Connection itemInserted;
        QMetaObject::Connection itemChanged;
    };

    [[nodiscard]] Node *nodeForIndex(const QModelIndex &index) const;
    [[nodiscard]] QModelIndex indexForNode(const Node *node, int column = 0) const;

    void clearModelState();
    void resetFromController();
    void attachLoadedThread(qodex::app::LoadedThread *loadedThread);
    void detachLoadedThread(qodex::app::LoadedThread *loadedThread);

    void onLoadedThreadAdded(qodex::app::LoadedThread *loadedThread, int row);
    void onLoadedThreadAboutToBeRemoved(qodex::app::LoadedThread *loadedThread, int row);
    void onLoadedThreadDestroyed(QObject *object);
    void onLoadedThreadSnapshotRebuilt(qodex::app::LoadedThread *loadedThread);
    void onLoadedThreadPresentationChanged(qodex::app::LoadedThread *loadedThread);
    void onTurnInserted(qodex::app::LoadedThread *loadedThread, const QString &turnId, int row);
    void onTurnChanged(qodex::app::LoadedThread *loadedThread, const QString &turnId);
    void onItemInserted(qodex::app::LoadedThread *loadedThread, const QString &turnId, const QString &itemId, int row);
    void onItemChanged(qodex::app::LoadedThread *loadedThread, const QString &turnId, const QString &itemId);

    [[nodiscard]] std::unique_ptr<Node> buildThreadNode(qodex::app::LoadedThread *loadedThread, Node *parent) const;
    [[nodiscard]] std::unique_ptr<Node> buildTurnNode(
        qodex::app::LoadedThread *loadedThread,
        const qodex::domain::threadmodel::Turn &turn,
        Node *parent
    ) const;
    [[nodiscard]] std::unique_ptr<Node> buildItemNode(
        qodex::app::LoadedThread *loadedThread,
        const QString &turnId,
        const qodex::domain::threadmodel::AbstractItem &item,
        Node *parent
    ) const;
    [[nodiscard]] std::unique_ptr<Node> buildPropertyNode(
        const QString &name,
        const QString &value,
        Node *parent
    ) const;

    void updateThreadNodeData(Node *threadNode) const;
    void updateTurnNodeData(Node *turnNode) const;
    void updateItemNodeData(Node *itemNode) const;
    void rebuildThreadChildren(Node *threadNode);
    void rebuildItemProperties(Node *itemNode);

    void registerSubtree(Node *node);
    void unregisterSubtree(Node *node);
    void reindexChildren(Node *parent, int fromRow = 0) const;

    [[nodiscard]] static QString turnKey(const QString &threadId, const QString &turnId);
    [[nodiscard]] static QString itemKey(const QString &threadId, const QString &turnId, const QString &itemId);

    qodex::app::SessionController *m_sessionController = nullptr;
    std::unique_ptr<Node> m_root;
    QHash<QString, Node *> m_threadNodesById;
    QHash<QString, Node *> m_turnNodesByKey;
    QHash<QString, Node *> m_itemNodesByKey;
    QHash<qodex::app::LoadedThread *, LoadedThreadConnections> m_loadedThreadConnections;
};

}  // namespace qodex::ui
