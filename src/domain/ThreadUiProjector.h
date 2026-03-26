#pragma once

#include <optional>

#include <QList>

#include "domain/threadmodel/Turn.h"
#include "qodex_to_ui.pb.h"

namespace qodex::domain {

class ThreadUiProjector {
public:
    [[nodiscard]] qodex::threadui::ipc::qodex_to_ui::AddItemsRequest projectTurns(
        const QList<const qodex::domain::threadmodel::Turn *> &turns
    ) const;
    [[nodiscard]] std::optional<qodex::threadui::ipc::common::Item> projectCompletedItem(
        const qodex::domain::threadmodel::AbstractItem &item
    ) const;

private:
    [[nodiscard]] bool appendCompletedItem(
        qodex::threadui::ipc::common::Item *displayItem,
        const qodex::domain::threadmodel::AbstractItem &item
    ) const;
    [[nodiscard]] QString summarizeNonMessageItem(const qodex::domain::threadmodel::AbstractItem &item) const;
    [[nodiscard]] QString flattenUserMessageContent(const QList<qodex::codex::Ref<qodex::codex::UserInput>> &content)
        const;
};

}  // namespace qodex::domain
