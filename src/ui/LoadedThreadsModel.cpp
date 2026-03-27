#include "ui/LoadedThreadsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <vector>
#include <utility>

#include "app/LoadedThread.h"
#include "app/SessionController.h"
#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/Turn.h"

namespace qodex::ui {

namespace {

QString kindName(const qodex::codex::ThreadItem::Kind kind) {
    switch (kind) {
    case qodex::codex::ThreadItem::Kind::UserMessage:
        return QStringLiteral("UserMessage");
    case qodex::codex::ThreadItem::Kind::AgentMessage:
        return QStringLiteral("AgentMessage");
    case qodex::codex::ThreadItem::Kind::Plan:
        return QStringLiteral("Plan");
    case qodex::codex::ThreadItem::Kind::Reasoning:
        return QStringLiteral("Reasoning");
    case qodex::codex::ThreadItem::Kind::CommandExecution:
        return QStringLiteral("CommandExecution");
    case qodex::codex::ThreadItem::Kind::FileChange:
        return QStringLiteral("FileChange");
    case qodex::codex::ThreadItem::Kind::McpToolCall:
        return QStringLiteral("McpToolCall");
    case qodex::codex::ThreadItem::Kind::DynamicToolCall:
        return QStringLiteral("DynamicToolCall");
    case qodex::codex::ThreadItem::Kind::CollabAgentToolCall:
        return QStringLiteral("CollabAgentToolCall");
    case qodex::codex::ThreadItem::Kind::WebSearch:
        return QStringLiteral("WebSearch");
    case qodex::codex::ThreadItem::Kind::ImageView:
        return QStringLiteral("ImageView");
    case qodex::codex::ThreadItem::Kind::ImageGeneration:
        return QStringLiteral("ImageGeneration");
    case qodex::codex::ThreadItem::Kind::EnteredReviewMode:
        return QStringLiteral("EnteredReviewMode");
    case qodex::codex::ThreadItem::Kind::ExitedReviewMode:
        return QStringLiteral("ExitedReviewMode");
    case qodex::codex::ThreadItem::Kind::ContextCompaction:
        return QStringLiteral("ContextCompaction");
    }

    return QStringLiteral("Unknown");
}

QString turnStatusName(const qodex::codex::TurnStatus status) {
    switch (status) {
    case qodex::codex::TurnStatus::Completed:
        return QStringLiteral("Completed");
    case qodex::codex::TurnStatus::Interrupted:
        return QStringLiteral("Interrupted");
    case qodex::codex::TurnStatus::Failed:
        return QStringLiteral("Failed");
    case qodex::codex::TurnStatus::InProgress:
        return QStringLiteral("InProgress");
    }

    return QStringLiteral("Unknown");
}

QString escapeAndElide(QString value) {
    value.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
    value.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    value.replace(QStringLiteral("\t"), QStringLiteral("\\t"));

    constexpr int kMaxLength = 120;
    if (value.size() <= kMaxLength) {
        return value;
    }

    const QString prefix = value.left(52);
    const QString suffix = value.right(52);
    return QStringLiteral("%1 ... %2").arg(prefix, suffix);
}

QString scalarToString(const QJsonValue &value) {
    if (value.isString()) {
        return escapeAndElide(value.toString());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    if (value.isUndefined()) {
        return QStringLiteral("undefined");
    }
    return escapeAndElide(QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
}

void flattenJson(const QString &path, const QJsonValue &value, QList<QPair<QString, QString>> *properties) {
    if (properties == nullptr) {
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            properties->append({path, QStringLiteral("{}")});
            return;
        }

        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : QStringLiteral("%1.%2").arg(path, it.key());
            flattenJson(childPath, it.value(), properties);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            properties->append({path, QStringLiteral("[]")});
            return;
        }

        for (qsizetype index = 0; index < array.size(); ++index) {
            flattenJson(QStringLiteral("%1[%2]").arg(path).arg(index), array.at(index), properties);
        }
        return;
    }

    properties->append({path, scalarToString(value)});
}

QList<QPair<QString, QString>> itemProperties(const qodex::domain::threadmodel::AbstractItem &item) {
    QList<QPair<QString, QString>> properties;
    properties.append({QStringLiteral("id"), item.id()});
    properties.append({QStringLiteral("kind"), kindName(item.kind())});
    properties.append({QStringLiteral("state"), item.isCompleted() ? QStringLiteral("Completed")
                                                                   : QStringLiteral("InProgress")});
    flattenJson(QString{}, item.properties(), &properties);
    return properties;
}

}  // namespace

struct LoadedThreadsModel::Node {
    NodeKind kind = NodeKind::Root;
    Node *parent = nullptr;
    int row = 0;
    QString label;
    QString value;
    QString threadId;
    QString turnId;
    QString itemId;
    qodex::app::LoadedThread *loadedThread = nullptr;
    std::vector<std::unique_ptr<Node>> children;
};

LoadedThreadsModel::LoadedThreadsModel(QObject *parent)
    : QAbstractItemModel(parent),
      m_root(std::make_unique<Node>()) {
    m_root->kind = NodeKind::Root;
}

LoadedThreadsModel::~LoadedThreadsModel() = default;

void LoadedThreadsModel::setSessionController(qodex::app::SessionController *sessionController) {
    if (m_sessionController == sessionController) {
        return;
    }

    if (m_sessionController != nullptr) {
        disconnect(m_sessionController, nullptr, this, nullptr);
        const QList<qodex::app::LoadedThread *> attachedThreads = m_loadedThreadConnections.keys();
        for (qodex::app::LoadedThread *loadedThread : attachedThreads) {
            detachLoadedThread(loadedThread);
        }
    }

    m_sessionController = sessionController;

    beginResetModel();
    clearModelState();
    resetFromController();
    endResetModel();

    if (m_sessionController != nullptr) {
        connect(
            m_sessionController,
            &qodex::app::SessionController::loadedThreadAdded,
            this,
            &LoadedThreadsModel::onLoadedThreadAdded
        );
        connect(
            m_sessionController,
            &qodex::app::SessionController::loadedThreadAboutToBeRemoved,
            this,
            &LoadedThreadsModel::onLoadedThreadAboutToBeRemoved
        );
    }
}

QModelIndex LoadedThreadsModel::index(const int row, const int column, const QModelIndex &parentIndex) const {
    if (row < 0 || column < 0 || column >= columnCount(parentIndex)) {
        return {};
    }

    const Node *parentNode = nodeForIndex(parentIndex);
    if (parentNode == nullptr || row >= parentNode->children.size()) {
        return {};
    }

    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex LoadedThreadsModel::parent(const QModelIndex &childIndex) const {
    if (!childIndex.isValid()) {
        return {};
    }

    const Node *childNode = nodeForIndex(childIndex);
    if (childNode == nullptr || childNode->parent == nullptr || childNode->parent == m_root.get()) {
        return {};
    }

    return createIndex(childNode->parent->row, 0, childNode->parent);
}

int LoadedThreadsModel::rowCount(const QModelIndex &parentIndex) const {
    if (parentIndex.column() > 0) {
        return 0;
    }

    const Node *parentNode = nodeForIndex(parentIndex);
    return parentNode == nullptr ? 0 : static_cast<int>(parentNode->children.size());
}

int LoadedThreadsModel::columnCount(const QModelIndex &parentIndex) const {
    Q_UNUSED(parentIndex);
    return 2;
}

QVariant LoadedThreadsModel::data(const QModelIndex &index, const int role) const {
    if (!index.isValid()) {
        return {};
    }

    const Node *node = nodeForIndex(index);
    if (node == nullptr) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        return index.column() == 0 ? node->label : node->value;
    }

    if (role == Qt::ToolTipRole) {
        return index.column() == 0 ? node->label : node->value;
    }

    return {};
}

QVariant LoadedThreadsModel::headerData(const int section, const Qt::Orientation orientation, const int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0:
        return QStringLiteral("Loaded Thread");
    case 1:
        return QStringLiteral("Value");
    default:
        return {};
    }
}

Qt::ItemFlags LoadedThreadsModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

LoadedThreadsModel::Node *LoadedThreadsModel::nodeForIndex(const QModelIndex &index) const {
    if (!index.isValid()) {
        return m_root.get();
    }

    return static_cast<Node *>(index.internalPointer());
}

QModelIndex LoadedThreadsModel::indexForNode(const Node *node, const int column) const {
    if (node == nullptr || node == m_root.get() || node->parent == nullptr) {
        return {};
    }

    return createIndex(node->row, column, const_cast<Node *>(node));
}

void LoadedThreadsModel::clearModelState() {
    const QList<qodex::app::LoadedThread *> attachedThreads = m_loadedThreadConnections.keys();
    for (qodex::app::LoadedThread *loadedThread : attachedThreads) {
        detachLoadedThread(loadedThread);
    }

    m_threadNodesById.clear();
    m_turnNodesByKey.clear();
    m_itemNodesByKey.clear();

    m_root = std::make_unique<Node>();
    m_root->kind = NodeKind::Root;
}

void LoadedThreadsModel::resetFromController() {
    if (m_sessionController == nullptr) {
        return;
    }

    const QList<const qodex::app::LoadedThread *> loadedThreads = m_sessionController->loadedThreads();
    m_root->children.reserve(loadedThreads.size());
    for (int row = 0; row < loadedThreads.size(); ++row) {
        auto *loadedThread = const_cast<qodex::app::LoadedThread *>(loadedThreads.at(row));
        if (loadedThread == nullptr) {
            continue;
        }

        std::unique_ptr<Node> threadNode = buildThreadNode(loadedThread, m_root.get());
        threadNode->row = row;
        m_threadNodesById.insert(loadedThread->threadId(), threadNode.get());
        registerSubtree(threadNode.get());
        m_root->children.push_back(std::move(threadNode));
        attachLoadedThread(loadedThread);
    }
}

void LoadedThreadsModel::attachLoadedThread(qodex::app::LoadedThread *loadedThread) {
    if (loadedThread == nullptr || m_loadedThreadConnections.contains(loadedThread)) {
        return;
    }

    LoadedThreadConnections connections;
    connections.destroyed = connect(
        loadedThread,
        &QObject::destroyed,
        this,
        &LoadedThreadsModel::onLoadedThreadDestroyed
    );
    connections.snapshotRebuilt = connect(
        loadedThread,
        &qodex::app::LoadedThread::snapshotRebuilt,
        this,
        [this, loadedThread] { onLoadedThreadSnapshotRebuilt(loadedThread); }
    );
    connections.threadPresentationChanged = connect(
        loadedThread,
        &qodex::app::LoadedThread::threadPresentationChanged,
        this,
        [this, loadedThread] { onLoadedThreadPresentationChanged(loadedThread); }
    );
    connections.turnInserted = connect(
        loadedThread,
        &qodex::app::LoadedThread::turnInserted,
        this,
        [this, loadedThread](const QString &turnId, const int row) { onTurnInserted(loadedThread, turnId, row); }
    );
    connections.turnChanged = connect(
        loadedThread,
        &qodex::app::LoadedThread::turnChanged,
        this,
        [this, loadedThread](const QString &turnId) { onTurnChanged(loadedThread, turnId); }
    );
    connections.itemInserted = connect(
        loadedThread,
        &qodex::app::LoadedThread::itemInserted,
        this,
        [this, loadedThread](const QString &turnId, const QString &itemId, const int row) {
            onItemInserted(loadedThread, turnId, itemId, row);
        }
    );
    connections.itemChanged = connect(
        loadedThread,
        &qodex::app::LoadedThread::itemChanged,
        this,
        [this, loadedThread](const QString &turnId, const QString &itemId) {
            onItemChanged(loadedThread, turnId, itemId);
        }
    );
    m_loadedThreadConnections.insert(loadedThread, connections);
}

void LoadedThreadsModel::detachLoadedThread(qodex::app::LoadedThread *loadedThread) {
    const auto it = m_loadedThreadConnections.find(loadedThread);
    if (it == m_loadedThreadConnections.end()) {
        return;
    }

    disconnect(it->destroyed);
    disconnect(it->snapshotRebuilt);
    disconnect(it->threadPresentationChanged);
    disconnect(it->turnInserted);
    disconnect(it->turnChanged);
    disconnect(it->itemInserted);
    disconnect(it->itemChanged);
    m_loadedThreadConnections.erase(it);
}

void LoadedThreadsModel::onLoadedThreadAdded(qodex::app::LoadedThread *loadedThread, int row) {
    if (loadedThread == nullptr || m_threadNodesById.contains(loadedThread->threadId())) {
        return;
    }

    row = std::clamp(row, 0, static_cast<int>(m_root->children.size()));
    beginInsertRows({}, row, row);
    std::unique_ptr<Node> threadNode = buildThreadNode(loadedThread, m_root.get());
    threadNode->row = row;
    Node *threadNodePtr = threadNode.get();
    m_root->children.insert(m_root->children.begin() + row, std::move(threadNode));
    reindexChildren(m_root.get(), row);
    m_threadNodesById.insert(loadedThread->threadId(), threadNodePtr);
    registerSubtree(threadNodePtr);
    endInsertRows();

    attachLoadedThread(loadedThread);
}

void LoadedThreadsModel::onLoadedThreadAboutToBeRemoved(qodex::app::LoadedThread *loadedThread, int row) {
    Q_UNUSED(row);

    if (loadedThread == nullptr) {
        return;
    }

    Node *threadNode = m_threadNodesById.value(loadedThread->threadId(), nullptr);
    if (threadNode == nullptr || threadNode->parent != m_root.get()) {
        detachLoadedThread(loadedThread);
        return;
    }

    row = threadNode->row;
    beginRemoveRows({}, row, row);
    unregisterSubtree(threadNode);
    m_threadNodesById.remove(loadedThread->threadId());
    m_root->children.erase(m_root->children.begin() + row);
    reindexChildren(m_root.get(), row);
    endRemoveRows();

    detachLoadedThread(loadedThread);
}

void LoadedThreadsModel::onLoadedThreadDestroyed(QObject *object) {
    auto *loadedThread = static_cast<qodex::app::LoadedThread *>(object);
    detachLoadedThread(loadedThread);
}

void LoadedThreadsModel::onLoadedThreadSnapshotRebuilt(qodex::app::LoadedThread *loadedThread) {
    Node *threadNode = loadedThread == nullptr ? nullptr : m_threadNodesById.value(loadedThread->threadId(), nullptr);
    if (threadNode == nullptr) {
        return;
    }

    updateThreadNodeData(threadNode);
    const QModelIndex threadIndex = indexForNode(threadNode);
    emit dataChanged(threadIndex, indexForNode(threadNode, 1));
    rebuildThreadChildren(threadNode);
}

void LoadedThreadsModel::onLoadedThreadPresentationChanged(qodex::app::LoadedThread *loadedThread) {
    Node *threadNode = loadedThread == nullptr ? nullptr : m_threadNodesById.value(loadedThread->threadId(), nullptr);
    if (threadNode == nullptr) {
        return;
    }

    updateThreadNodeData(threadNode);
    emit dataChanged(indexForNode(threadNode), indexForNode(threadNode, 1));
}

void LoadedThreadsModel::onTurnInserted(qodex::app::LoadedThread *loadedThread, const QString &turnId, int row) {
    if (loadedThread == nullptr || turnId.isEmpty()) {
        return;
    }

    Node *threadNode = m_threadNodesById.value(loadedThread->threadId(), nullptr);
    const auto *turn = loadedThread->turnForId(turnId);
    if (threadNode == nullptr || turn == nullptr) {
        return;
    }

    row = std::clamp(row, 0, static_cast<int>(threadNode->children.size()));
    const QModelIndex threadIndex = indexForNode(threadNode);
    beginInsertRows(threadIndex, row, row);
    std::unique_ptr<Node> turnNode = buildTurnNode(loadedThread, *turn, threadNode);
    turnNode->row = row;
    Node *turnNodePtr = turnNode.get();
    threadNode->children.insert(threadNode->children.begin() + row, std::move(turnNode));
    reindexChildren(threadNode, row);
    registerSubtree(turnNodePtr);
    endInsertRows();
}

void LoadedThreadsModel::onTurnChanged(qodex::app::LoadedThread *loadedThread, const QString &turnId) {
    if (loadedThread == nullptr || turnId.isEmpty()) {
        return;
    }

    Node *turnNode = m_turnNodesByKey.value(turnKey(loadedThread->threadId(), turnId), nullptr);
    if (turnNode == nullptr) {
        return;
    }

    updateTurnNodeData(turnNode);
    emit dataChanged(indexForNode(turnNode), indexForNode(turnNode, 1));
}

void LoadedThreadsModel::onItemInserted(
    qodex::app::LoadedThread *loadedThread,
    const QString &turnId,
    const QString &itemId,
    int row
) {
    if (loadedThread == nullptr || turnId.isEmpty() || itemId.isEmpty()) {
        return;
    }

    Node *turnNode = m_turnNodesByKey.value(turnKey(loadedThread->threadId(), turnId), nullptr);
    const auto *turn = loadedThread->turnForId(turnId);
    const auto *item = turn == nullptr ? nullptr : turn->itemById(itemId);
    if (turnNode == nullptr || turn == nullptr || item == nullptr) {
        return;
    }

    row = std::clamp(row, 0, static_cast<int>(turnNode->children.size()));
    const QModelIndex turnIndex = indexForNode(turnNode);
    beginInsertRows(turnIndex, row, row);
    std::unique_ptr<Node> itemNode = buildItemNode(loadedThread, turnId, *item, turnNode);
    itemNode->row = row;
    Node *itemNodePtr = itemNode.get();
    turnNode->children.insert(turnNode->children.begin() + row, std::move(itemNode));
    reindexChildren(turnNode, row);
    registerSubtree(itemNodePtr);
    endInsertRows();
}

void LoadedThreadsModel::onItemChanged(
    qodex::app::LoadedThread *loadedThread,
    const QString &turnId,
    const QString &itemId
) {
    if (loadedThread == nullptr || turnId.isEmpty() || itemId.isEmpty()) {
        return;
    }

    Node *itemNode = m_itemNodesByKey.value(itemKey(loadedThread->threadId(), turnId, itemId), nullptr);
    const auto *turn = loadedThread->turnForId(turnId);
    const auto *item = turn == nullptr ? nullptr : turn->itemById(itemId);
    if (itemNode == nullptr || item == nullptr) {
        return;
    }

    updateItemNodeData(itemNode);
    emit dataChanged(indexForNode(itemNode), indexForNode(itemNode, 1));
    rebuildItemProperties(itemNode);
}

std::unique_ptr<LoadedThreadsModel::Node> LoadedThreadsModel::buildThreadNode(
    qodex::app::LoadedThread *loadedThread,
    Node *parent
) const {
    auto threadNode = std::make_unique<Node>();
    threadNode->kind = NodeKind::Thread;
    threadNode->parent = parent;
    threadNode->threadId = loadedThread->threadId();
    threadNode->loadedThread = loadedThread;
    updateThreadNodeData(threadNode.get());

    const QList<const qodex::domain::threadmodel::Turn *> turns = loadedThread->orderedTurns();
    threadNode->children.reserve(turns.size());
    for (int row = 0; row < turns.size(); ++row) {
        const auto *turn = turns.at(row);
        if (turn == nullptr) {
            continue;
        }

        std::unique_ptr<Node> turnNode = buildTurnNode(loadedThread, *turn, threadNode.get());
        turnNode->row = static_cast<int>(threadNode->children.size());
        threadNode->children.push_back(std::move(turnNode));
    }

    return threadNode;
}

std::unique_ptr<LoadedThreadsModel::Node> LoadedThreadsModel::buildTurnNode(
    qodex::app::LoadedThread *loadedThread,
    const qodex::domain::threadmodel::Turn &turn,
    Node *parent
) const {
    auto turnNode = std::make_unique<Node>();
    turnNode->kind = NodeKind::Turn;
    turnNode->parent = parent;
    turnNode->threadId = loadedThread->threadId();
    turnNode->turnId = turn.id();
    turnNode->loadedThread = loadedThread;
    updateTurnNodeData(turnNode.get());

    const QList<const qodex::domain::threadmodel::AbstractItem *> items = turn.orderedItems();
    turnNode->children.reserve(items.size());
    for (const auto *item : items) {
        if (item == nullptr) {
            continue;
        }

        std::unique_ptr<Node> itemNode = buildItemNode(loadedThread, turn.id(), *item, turnNode.get());
        itemNode->row = static_cast<int>(turnNode->children.size());
        turnNode->children.push_back(std::move(itemNode));
    }

    return turnNode;
}

std::unique_ptr<LoadedThreadsModel::Node> LoadedThreadsModel::buildItemNode(
    qodex::app::LoadedThread *loadedThread,
    const QString &turnId,
    const qodex::domain::threadmodel::AbstractItem &item,
    Node *parent
) const {
    auto itemNode = std::make_unique<Node>();
    itemNode->kind = NodeKind::Item;
    itemNode->parent = parent;
    itemNode->threadId = loadedThread->threadId();
    itemNode->turnId = turnId;
    itemNode->itemId = item.id();
    itemNode->loadedThread = loadedThread;
    updateItemNodeData(itemNode.get());

    const QList<QPair<QString, QString>> properties = itemProperties(item);
    itemNode->children.reserve(properties.size());
    for (const auto &property : properties) {
        std::unique_ptr<Node> propertyNode = buildPropertyNode(property.first, property.second, itemNode.get());
        propertyNode->row = static_cast<int>(itemNode->children.size());
        itemNode->children.push_back(std::move(propertyNode));
    }

    return itemNode;
}

std::unique_ptr<LoadedThreadsModel::Node> LoadedThreadsModel::buildPropertyNode(
    const QString &name,
    const QString &value,
    Node *parent
) const {
    auto propertyNode = std::make_unique<Node>();
    propertyNode->kind = NodeKind::Property;
    propertyNode->parent = parent;
    propertyNode->label = name;
    propertyNode->value = value;
    return propertyNode;
}

void LoadedThreadsModel::updateThreadNodeData(Node *threadNode) const {
    if (threadNode == nullptr || threadNode->loadedThread == nullptr) {
        return;
    }

    const QString threadTitle = threadNode->loadedThread->title().isEmpty()
        ? threadNode->loadedThread->threadId()
        : threadNode->loadedThread->title();
    threadNode->label = QStringLiteral("Thread %1").arg(threadTitle);
    threadNode->value = QStringLiteral("id=%1 activeTurn=%2")
                            .arg(
                                threadNode->loadedThread->threadId(),
                                threadNode->loadedThread->activeTurnId().isEmpty()
                                    ? QStringLiteral("—")
                                    : threadNode->loadedThread->activeTurnId()
                            );
}

void LoadedThreadsModel::updateTurnNodeData(Node *turnNode) const {
    if (turnNode == nullptr || turnNode->loadedThread == nullptr) {
        return;
    }

    const auto *turn = turnNode->loadedThread->turnForId(turnNode->turnId);
    if (turn == nullptr) {
        return;
    }

    turnNode->label = QStringLiteral("Turn %1").arg(turn->id());
    turnNode->value = QStringLiteral("status=%1").arg(turnStatusName(turn->status()));
}

void LoadedThreadsModel::updateItemNodeData(Node *itemNode) const {
    if (itemNode == nullptr || itemNode->loadedThread == nullptr) {
        return;
    }

    const auto *turn = itemNode->loadedThread->turnForId(itemNode->turnId);
    const auto *item = turn == nullptr ? nullptr : turn->itemById(itemNode->itemId);
    if (item == nullptr) {
        return;
    }

    itemNode->label = QStringLiteral("%1 %2").arg(kindName(item->kind()), item->id());
    itemNode->value = item->isCompleted() ? QStringLiteral("Completed") : QStringLiteral("InProgress");
}

void LoadedThreadsModel::rebuildThreadChildren(Node *threadNode) {
    if (threadNode == nullptr || threadNode->loadedThread == nullptr) {
        return;
    }

    const QModelIndex threadIndex = indexForNode(threadNode);
    if (!threadNode->children.empty()) {
        beginRemoveRows(threadIndex, 0, static_cast<int>(threadNode->children.size()) - 1);
        for (const auto &child : std::as_const(threadNode->children)) {
            unregisterSubtree(child.get());
        }
        threadNode->children.clear();
        endRemoveRows();
    }

    const QList<const qodex::domain::threadmodel::Turn *> turns = threadNode->loadedThread->orderedTurns();
    if (turns.isEmpty()) {
        return;
    }

    beginInsertRows(threadIndex, 0, turns.size() - 1);
    threadNode->children.reserve(turns.size());
    for (const auto *turn : turns) {
        if (turn == nullptr) {
            continue;
        }

        std::unique_ptr<Node> turnNode = buildTurnNode(threadNode->loadedThread, *turn, threadNode);
        turnNode->row = static_cast<int>(threadNode->children.size());
        registerSubtree(turnNode.get());
        threadNode->children.push_back(std::move(turnNode));
    }
    endInsertRows();
}

void LoadedThreadsModel::rebuildItemProperties(Node *itemNode) {
    if (itemNode == nullptr || itemNode->loadedThread == nullptr) {
        return;
    }

    const auto *turn = itemNode->loadedThread->turnForId(itemNode->turnId);
    const auto *item = turn == nullptr ? nullptr : turn->itemById(itemNode->itemId);
    if (item == nullptr) {
        return;
    }

    const QModelIndex itemIndex = indexForNode(itemNode);
    if (!itemNode->children.empty()) {
        beginRemoveRows(itemIndex, 0, static_cast<int>(itemNode->children.size()) - 1);
        itemNode->children.clear();
        endRemoveRows();
    }

    const QList<QPair<QString, QString>> properties = itemProperties(*item);
    if (properties.isEmpty()) {
        return;
    }

    beginInsertRows(itemIndex, 0, properties.size() - 1);
    itemNode->children.reserve(properties.size());
    for (const auto &property : properties) {
        std::unique_ptr<Node> propertyNode = buildPropertyNode(property.first, property.second, itemNode);
        propertyNode->row = static_cast<int>(itemNode->children.size());
        itemNode->children.push_back(std::move(propertyNode));
    }
    endInsertRows();
}

void LoadedThreadsModel::registerSubtree(Node *node) {
    if (node == nullptr) {
        return;
    }

    switch (node->kind) {
    case NodeKind::Turn:
        m_turnNodesByKey.insert(turnKey(node->threadId, node->turnId), node);
        break;
    case NodeKind::Item:
        m_itemNodesByKey.insert(itemKey(node->threadId, node->turnId, node->itemId), node);
        break;
    case NodeKind::Root:
    case NodeKind::Thread:
    case NodeKind::Property:
        break;
    }

    for (const auto &child : std::as_const(node->children)) {
        registerSubtree(child.get());
    }
}

void LoadedThreadsModel::unregisterSubtree(Node *node) {
    if (node == nullptr) {
        return;
    }

    for (const auto &child : std::as_const(node->children)) {
        unregisterSubtree(child.get());
    }

    switch (node->kind) {
    case NodeKind::Turn:
        m_turnNodesByKey.remove(turnKey(node->threadId, node->turnId));
        break;
    case NodeKind::Item:
        m_itemNodesByKey.remove(itemKey(node->threadId, node->turnId, node->itemId));
        break;
    case NodeKind::Root:
    case NodeKind::Thread:
    case NodeKind::Property:
        break;
    }
}

void LoadedThreadsModel::reindexChildren(Node *parent, const int fromRow) const {
    if (parent == nullptr) {
        return;
    }

    for (int row = std::max(0, fromRow); row < static_cast<int>(parent->children.size()); ++row) {
        parent->children[static_cast<std::size_t>(row)]->row = row;
    }
}

QString LoadedThreadsModel::turnKey(const QString &threadId, const QString &turnId) {
    return QStringLiteral("%1\x1f%2").arg(threadId, turnId);
}

QString LoadedThreadsModel::itemKey(const QString &threadId, const QString &turnId, const QString &itemId) {
    return QStringLiteral("%1\x1f%2\x1f%3").arg(threadId, turnId, itemId);
}

}  // namespace qodex::ui
