#include "domain/threadmodel/Turn.h"

#include <memory>
#include <type_traits>
#include <utility>

#include "domain/threadmodel/AbstractItem.h"
#include "domain/threadmodel/CompletedAgentMessage.h"
#include "domain/threadmodel/CompletedCollabAgentToolCall.h"
#include "domain/threadmodel/CompletedCommandExecution.h"
#include "domain/threadmodel/CompletedContextCompaction.h"
#include "domain/threadmodel/CompletedDynamicToolCall.h"
#include "domain/threadmodel/CompletedEnteredReviewMode.h"
#include "domain/threadmodel/CompletedExitedReviewMode.h"
#include "domain/threadmodel/CompletedFileChange.h"
#include "domain/threadmodel/CompletedImageGeneration.h"
#include "domain/threadmodel/CompletedImageView.h"
#include "domain/threadmodel/CompletedItem.h"
#include "domain/threadmodel/CompletedMcpToolCall.h"
#include "domain/threadmodel/CompletedPlan.h"
#include "domain/threadmodel/CompletedReasoning.h"
#include "domain/threadmodel/CompletedUserMessage.h"
#include "domain/threadmodel/CompletedWebSearch.h"
#include "domain/threadmodel/InprogressAgentMessage.h"
#include "domain/threadmodel/InprogressCollabAgentToolCall.h"
#include "domain/threadmodel/InprogressCommandExecution.h"
#include "domain/threadmodel/InprogressContextCompaction.h"
#include "domain/threadmodel/InprogressDynamicToolCall.h"
#include "domain/threadmodel/InprogressEnteredReviewMode.h"
#include "domain/threadmodel/InprogressExitedReviewMode.h"
#include "domain/threadmodel/InprogressFileChange.h"
#include "domain/threadmodel/InprogressImageGeneration.h"
#include "domain/threadmodel/InprogressImageView.h"
#include "domain/threadmodel/InprogressItem.h"
#include "domain/threadmodel/InprogressMcpToolCall.h"
#include "domain/threadmodel/InprogressPlan.h"
#include "domain/threadmodel/InprogressReasoning.h"
#include "domain/threadmodel/InprogressUserMessage.h"
#include "domain/threadmodel/InprogressWebSearch.h"

namespace qodex::domain::threadmodel {

namespace {

using qodex::codex::Ref;
using qodex::codex::ThreadItem;

template <typename PayloadT, typename ItemT>
std::unique_ptr<ItemT> makeItemFromRef(const Ref<PayloadT> &payloadRef) {
    if (!payloadRef) {
        return nullptr;
    }

    return std::make_unique<ItemT>(*payloadRef);
}

std::unique_ptr<CompletedItem> makeCompletedItem(const ThreadItem &item) {
    switch (item.kind) {
    case ThreadItem::Kind::UserMessage:
        return makeItemFromRef<qodex::codex::ThreadItemUserMessage, CompletedUserMessage>(
            std::get<Ref<qodex::codex::ThreadItemUserMessage>>(item.payload)
        );
    case ThreadItem::Kind::AgentMessage:
        return makeItemFromRef<qodex::codex::ThreadItemAgentMessage, CompletedAgentMessage>(
            std::get<Ref<qodex::codex::ThreadItemAgentMessage>>(item.payload)
        );
    case ThreadItem::Kind::Plan:
        return makeItemFromRef<qodex::codex::ThreadItemPlan, CompletedPlan>(
            std::get<Ref<qodex::codex::ThreadItemPlan>>(item.payload)
        );
    case ThreadItem::Kind::Reasoning:
        return makeItemFromRef<qodex::codex::ThreadItemReasoning, CompletedReasoning>(
            std::get<Ref<qodex::codex::ThreadItemReasoning>>(item.payload)
        );
    case ThreadItem::Kind::CommandExecution:
        return makeItemFromRef<qodex::codex::ThreadItemCommandExecution, CompletedCommandExecution>(
            std::get<Ref<qodex::codex::ThreadItemCommandExecution>>(item.payload)
        );
    case ThreadItem::Kind::FileChange:
        return makeItemFromRef<qodex::codex::ThreadItemFileChange, CompletedFileChange>(
            std::get<Ref<qodex::codex::ThreadItemFileChange>>(item.payload)
        );
    case ThreadItem::Kind::McpToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemMcpToolCall, CompletedMcpToolCall>(
            std::get<Ref<qodex::codex::ThreadItemMcpToolCall>>(item.payload)
        );
    case ThreadItem::Kind::DynamicToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemDynamicToolCall, CompletedDynamicToolCall>(
            std::get<Ref<qodex::codex::ThreadItemDynamicToolCall>>(item.payload)
        );
    case ThreadItem::Kind::CollabAgentToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemCollabAgentToolCall, CompletedCollabAgentToolCall>(
            std::get<Ref<qodex::codex::ThreadItemCollabAgentToolCall>>(item.payload)
        );
    case ThreadItem::Kind::WebSearch:
        return makeItemFromRef<qodex::codex::ThreadItemWebSearch, CompletedWebSearch>(
            std::get<Ref<qodex::codex::ThreadItemWebSearch>>(item.payload)
        );
    case ThreadItem::Kind::ImageView:
        return makeItemFromRef<qodex::codex::ThreadItemImageView, CompletedImageView>(
            std::get<Ref<qodex::codex::ThreadItemImageView>>(item.payload)
        );
    case ThreadItem::Kind::ImageGeneration:
        return makeItemFromRef<qodex::codex::ThreadItemImageGeneration, CompletedImageGeneration>(
            std::get<Ref<qodex::codex::ThreadItemImageGeneration>>(item.payload)
        );
    case ThreadItem::Kind::EnteredReviewMode:
        return makeItemFromRef<qodex::codex::ThreadItemEnteredReviewMode, CompletedEnteredReviewMode>(
            std::get<Ref<qodex::codex::ThreadItemEnteredReviewMode>>(item.payload)
        );
    case ThreadItem::Kind::ExitedReviewMode:
        return makeItemFromRef<qodex::codex::ThreadItemExitedReviewMode, CompletedExitedReviewMode>(
            std::get<Ref<qodex::codex::ThreadItemExitedReviewMode>>(item.payload)
        );
    case ThreadItem::Kind::ContextCompaction:
        return makeItemFromRef<qodex::codex::ThreadItemContextCompaction, CompletedContextCompaction>(
            std::get<Ref<qodex::codex::ThreadItemContextCompaction>>(item.payload)
        );
    }

    return nullptr;
}

std::unique_ptr<InprogressItem> makeInprogressItem(const ThreadItem &item) {
    switch (item.kind) {
    case ThreadItem::Kind::UserMessage:
        return makeItemFromRef<qodex::codex::ThreadItemUserMessage, InprogressUserMessage>(
            std::get<Ref<qodex::codex::ThreadItemUserMessage>>(item.payload)
        );
    case ThreadItem::Kind::AgentMessage:
        return makeItemFromRef<qodex::codex::ThreadItemAgentMessage, InprogressAgentMessage>(
            std::get<Ref<qodex::codex::ThreadItemAgentMessage>>(item.payload)
        );
    case ThreadItem::Kind::Plan:
        return makeItemFromRef<qodex::codex::ThreadItemPlan, InprogressPlan>(
            std::get<Ref<qodex::codex::ThreadItemPlan>>(item.payload)
        );
    case ThreadItem::Kind::Reasoning:
        return makeItemFromRef<qodex::codex::ThreadItemReasoning, InprogressReasoning>(
            std::get<Ref<qodex::codex::ThreadItemReasoning>>(item.payload)
        );
    case ThreadItem::Kind::CommandExecution:
        return makeItemFromRef<qodex::codex::ThreadItemCommandExecution, InprogressCommandExecution>(
            std::get<Ref<qodex::codex::ThreadItemCommandExecution>>(item.payload)
        );
    case ThreadItem::Kind::FileChange:
        return makeItemFromRef<qodex::codex::ThreadItemFileChange, InprogressFileChange>(
            std::get<Ref<qodex::codex::ThreadItemFileChange>>(item.payload)
        );
    case ThreadItem::Kind::McpToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemMcpToolCall, InprogressMcpToolCall>(
            std::get<Ref<qodex::codex::ThreadItemMcpToolCall>>(item.payload)
        );
    case ThreadItem::Kind::DynamicToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemDynamicToolCall, InprogressDynamicToolCall>(
            std::get<Ref<qodex::codex::ThreadItemDynamicToolCall>>(item.payload)
        );
    case ThreadItem::Kind::CollabAgentToolCall:
        return makeItemFromRef<qodex::codex::ThreadItemCollabAgentToolCall, InprogressCollabAgentToolCall>(
            std::get<Ref<qodex::codex::ThreadItemCollabAgentToolCall>>(item.payload)
        );
    case ThreadItem::Kind::WebSearch:
        return makeItemFromRef<qodex::codex::ThreadItemWebSearch, InprogressWebSearch>(
            std::get<Ref<qodex::codex::ThreadItemWebSearch>>(item.payload)
        );
    case ThreadItem::Kind::ImageView:
        return makeItemFromRef<qodex::codex::ThreadItemImageView, InprogressImageView>(
            std::get<Ref<qodex::codex::ThreadItemImageView>>(item.payload)
        );
    case ThreadItem::Kind::ImageGeneration:
        return makeItemFromRef<qodex::codex::ThreadItemImageGeneration, InprogressImageGeneration>(
            std::get<Ref<qodex::codex::ThreadItemImageGeneration>>(item.payload)
        );
    case ThreadItem::Kind::EnteredReviewMode:
        return makeItemFromRef<qodex::codex::ThreadItemEnteredReviewMode, InprogressEnteredReviewMode>(
            std::get<Ref<qodex::codex::ThreadItemEnteredReviewMode>>(item.payload)
        );
    case ThreadItem::Kind::ExitedReviewMode:
        return makeItemFromRef<qodex::codex::ThreadItemExitedReviewMode, InprogressExitedReviewMode>(
            std::get<Ref<qodex::codex::ThreadItemExitedReviewMode>>(item.payload)
        );
    case ThreadItem::Kind::ContextCompaction:
        return makeItemFromRef<qodex::codex::ThreadItemContextCompaction, InprogressContextCompaction>(
            std::get<Ref<qodex::codex::ThreadItemContextCompaction>>(item.payload)
        );
    }

    return nullptr;
}

qodex::codex::ThreadItemAgentMessage makeEmptyAgentMessage(const QString &itemId) {
    qodex::codex::ThreadItemAgentMessage item;
    item.id = itemId;
    return item;
}

qodex::codex::ThreadItemCommandExecution makeEmptyCommandExecution(const QString &itemId) {
    qodex::codex::ThreadItemCommandExecution item;
    item.id = itemId;
    return item;
}

qodex::codex::ThreadItemFileChange makeEmptyFileChange(const QString &itemId) {
    qodex::codex::ThreadItemFileChange item;
    item.id = itemId;
    return item;
}

qodex::codex::ThreadItemMcpToolCall makeEmptyMcpToolCall(const QString &itemId) {
    qodex::codex::ThreadItemMcpToolCall item;
    item.id = itemId;
    return item;
}

qodex::codex::ThreadItemPlan makeEmptyPlan(const QString &itemId) {
    qodex::codex::ThreadItemPlan item;
    item.id = itemId;
    return item;
}

qodex::codex::ThreadItemReasoning makeEmptyReasoning(const QString &itemId) {
    qodex::codex::ThreadItemReasoning item;
    item.id = itemId;
    return item;
}

}  // namespace

Turn::Turn(QString id)
    : m_id(std::move(id)) {
}

Turn::~Turn() = default;

const QString &Turn::id() const {
    return m_id;
}

qodex::codex::TurnStatus Turn::status() const {
    return m_status;
}

const qodex::codex::Nullable<qodex::codex::Ref<qodex::codex::TurnError>> &Turn::error() const {
    return m_error;
}

QList<const AbstractItem *> Turn::orderedItems() const {
    QList<const AbstractItem *> items;
    items.reserve(m_itemOrder.size());

    for (const QString &itemId : m_itemOrder) {
        const auto it = m_itemsById.find(itemId);
        if (it == m_itemsById.end() || it->second == nullptr) {
            continue;
        }
        items.append(it->second.get());
    }

    return items;
}

const AbstractItem *Turn::itemById(const QString &itemId) const {
    const auto it = m_itemsById.find(itemId);
    return it == m_itemsById.end() ? nullptr : it->second.get();
}

AbstractItem *Turn::itemById(const QString &itemId) {
    const auto it = m_itemsById.find(itemId);
    return it == m_itemsById.end() ? nullptr : it->second.get();
}

void Turn::applySnapshot(const qodex::codex::Turn &turn) {
    if (turn.id != m_id) {
        return;
    }

    applyMetadata(turn);
    clearItems();

    for (const qodex::codex::Ref<qodex::codex::ThreadItem> &item : turn.items) {
        if (!item) {
            continue;
        }

        if (auto completedItem = makeCompletedItem(*item)) {
            upsertItem(std::move(completedItem));
        }
    }
}

void Turn::applyMetadata(const qodex::codex::Turn &turn) {
    if (turn.id != m_id) {
        return;
    }

    m_status = turn.status;
    m_error = turn.error;
}

InprogressItem *Turn::applyStartedItem(const qodex::codex::ThreadItem &item) {
    if (auto startedItem = makeInprogressItem(item)) {
        InprogressItem *itemPtr = startedItem.get();
        upsertItem(std::move(startedItem));
        return itemPtr;
    }

    return nullptr;
}

CompletedItem *Turn::applyCompletedItem(const qodex::codex::ThreadItem &item) {
    if (auto completedItem = makeCompletedItem(item)) {
        CompletedItem *itemPtr = completedItem.get();
        upsertItem(std::move(completedItem));
        return itemPtr;
    }

    return nullptr;
}

bool Turn::applyAgentMessageDelta(const QString &itemId, const QString &delta) {
    if (auto *item = dynamic_cast<InprogressAgentMessage *>(ensureInprogressItem<InprogressAgentMessage>(itemId))) {
        item->appendDelta(delta);
        return true;
    }

    return false;
}

bool Turn::applyCommandExecutionOutputDelta(const QString &itemId, const QString &delta) {
    if (auto *item =
            dynamic_cast<InprogressCommandExecution *>(ensureInprogressItem<InprogressCommandExecution>(itemId))) {
        item->appendOutputDelta(delta);
        return true;
    }

    return false;
}

bool Turn::applyCommandExecutionTerminalInteraction(const QString &itemId, const QString &processId, const QString &stdin) {
    if (auto *item =
            dynamic_cast<InprogressCommandExecution *>(ensureInprogressItem<InprogressCommandExecution>(itemId))) {
        item->recordTerminalInteraction(processId, stdin);
        return true;
    }

    return false;
}

bool Turn::applyFileChangeOutputDelta(const QString &itemId, const QString &delta) {
    if (auto *item = dynamic_cast<InprogressFileChange *>(ensureInprogressItem<InprogressFileChange>(itemId))) {
        item->appendOutputDelta(delta);
        return true;
    }

    return false;
}

bool Turn::applyMcpToolCallProgress(const QString &itemId, const QString &message) {
    if (auto *item = dynamic_cast<InprogressMcpToolCall *>(ensureInprogressItem<InprogressMcpToolCall>(itemId))) {
        item->appendProgressMessage(message);
        return true;
    }

    return false;
}

bool Turn::applyPlanDelta(const QString &itemId, const QString &delta) {
    if (auto *item = dynamic_cast<InprogressPlan *>(ensureInprogressItem<InprogressPlan>(itemId))) {
        item->appendDelta(delta);
        return true;
    }

    return false;
}

bool Turn::applyReasoningSummaryPartAdded(const QString &itemId, const qint64 summaryIndex) {
    if (auto *item = dynamic_cast<InprogressReasoning *>(ensureInprogressItem<InprogressReasoning>(itemId))) {
        item->addSummaryPart(summaryIndex);
        return true;
    }

    return false;
}

bool Turn::applyReasoningSummaryTextDelta(const QString &itemId, const qint64 summaryIndex, const QString &delta) {
    if (auto *item = dynamic_cast<InprogressReasoning *>(ensureInprogressItem<InprogressReasoning>(itemId))) {
        item->appendSummaryTextDelta(summaryIndex, delta);
        return true;
    }

    return false;
}

bool Turn::applyReasoningTextDelta(const QString &itemId, const qint64 contentIndex, const QString &delta) {
    if (auto *item = dynamic_cast<InprogressReasoning *>(ensureInprogressItem<InprogressReasoning>(itemId))) {
        item->appendContentDelta(contentIndex, delta);
        return true;
    }

    return false;
}

void Turn::clearItems() {
    m_itemOrder.clear();
    m_itemsById.clear();
}

void Turn::upsertItem(std::unique_ptr<AbstractItem> item) {
    if (item == nullptr) {
        return;
    }

    const QString itemId = item->id();
    if (!m_itemsById.contains(itemId)) {
        m_itemOrder.append(itemId);
    }

    m_itemsById[itemId] = std::move(item);
}

template <typename ItemT>
ItemT *Turn::ensureInprogressItem(const QString &itemId) {
    if (auto *existingItem = dynamic_cast<ItemT *>(itemById(itemId))) {
        return existingItem;
    }

    std::unique_ptr<AbstractItem> item;
    if constexpr (std::is_same_v<ItemT, InprogressAgentMessage>) {
        item = std::make_unique<InprogressAgentMessage>(makeEmptyAgentMessage(itemId));
    } else if constexpr (std::is_same_v<ItemT, InprogressCommandExecution>) {
        item = std::make_unique<InprogressCommandExecution>(makeEmptyCommandExecution(itemId));
    } else if constexpr (std::is_same_v<ItemT, InprogressFileChange>) {
        item = std::make_unique<InprogressFileChange>(makeEmptyFileChange(itemId));
    } else if constexpr (std::is_same_v<ItemT, InprogressMcpToolCall>) {
        item = std::make_unique<InprogressMcpToolCall>(makeEmptyMcpToolCall(itemId));
    } else if constexpr (std::is_same_v<ItemT, InprogressPlan>) {
        item = std::make_unique<InprogressPlan>(makeEmptyPlan(itemId));
    } else if constexpr (std::is_same_v<ItemT, InprogressReasoning>) {
        item = std::make_unique<InprogressReasoning>(makeEmptyReasoning(itemId));
    } else {
        return nullptr;
    }

    ItemT *itemPtr = dynamic_cast<ItemT *>(item.get());
    upsertItem(std::move(item));
    return itemPtr;
}

template InprogressAgentMessage *Turn::ensureInprogressItem<InprogressAgentMessage>(const QString &itemId);
template InprogressCommandExecution *Turn::ensureInprogressItem<InprogressCommandExecution>(const QString &itemId);
template InprogressFileChange *Turn::ensureInprogressItem<InprogressFileChange>(const QString &itemId);
template InprogressMcpToolCall *Turn::ensureInprogressItem<InprogressMcpToolCall>(const QString &itemId);
template InprogressPlan *Turn::ensureInprogressItem<InprogressPlan>(const QString &itemId);
template InprogressReasoning *Turn::ensureInprogressItem<InprogressReasoning>(const QString &itemId);

}  // namespace qodex::domain::threadmodel
