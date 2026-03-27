#pragma once

#include <map>
#include <memory>

#include <QList>
#include <QString>
#include <QStringList>

#include "CodexProtocol.h"

namespace qodex::domain::threadmodel {

class AbstractItem;
class CompletedItem;
class InprogressItem;

class Turn {
public:
    explicit Turn(QString id);
    ~Turn();

    [[nodiscard]] const QString &id() const;
    [[nodiscard]] qodex::codex::TurnStatus status() const;
    [[nodiscard]] const qodex::codex::Nullable<qodex::codex::Ref<qodex::codex::TurnError>> &error() const;
    [[nodiscard]] QList<const AbstractItem *> orderedItems() const;
    [[nodiscard]] int itemRow(const QString &itemId) const;
    [[nodiscard]] const AbstractItem *itemById(const QString &itemId) const;
    [[nodiscard]] AbstractItem *itemById(const QString &itemId);

    void applySnapshot(const qodex::codex::Turn &turn);
    void applyMetadata(const qodex::codex::Turn &turn);
    [[nodiscard]] InprogressItem *applyStartedItem(const qodex::codex::ThreadItem &item);
    [[nodiscard]] CompletedItem *applyCompletedItem(const qodex::codex::ThreadItem &item);
    bool applyAgentMessageDelta(const QString &itemId, const QString &delta);
    bool applyCommandExecutionOutputDelta(const QString &itemId, const QString &delta);
    bool applyCommandExecutionTerminalInteraction(const QString &itemId, const QString &processId, const QString &stdin);
    bool applyFileChangeOutputDelta(const QString &itemId, const QString &delta);
    bool applyMcpToolCallProgress(const QString &itemId, const QString &message);
    bool applyPlanDelta(const QString &itemId, const QString &delta);
    bool applyReasoningSummaryPartAdded(const QString &itemId, qint64 summaryIndex);
    bool applyReasoningSummaryTextDelta(const QString &itemId, qint64 summaryIndex, const QString &delta);
    bool applyReasoningTextDelta(const QString &itemId, qint64 contentIndex, const QString &delta);

private:
    void clearItems();
    void upsertItem(std::unique_ptr<AbstractItem> item);

    template <typename ItemT>
    [[nodiscard]] ItemT *ensureInprogressItem(const QString &itemId);

    QString m_id;
    qodex::codex::TurnStatus m_status = qodex::codex::TurnStatus::Interrupted;
    qodex::codex::Nullable<qodex::codex::Ref<qodex::codex::TurnError>> m_error;
    QStringList m_itemOrder;
    std::map<QString, std::unique_ptr<AbstractItem>> m_itemsById;
};

}  // namespace qodex::domain::threadmodel
