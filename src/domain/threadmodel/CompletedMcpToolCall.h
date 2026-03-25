#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedMcpToolCall final
    : public PayloadCompletedItem<qodex::codex::ThreadItemMcpToolCall, qodex::codex::ThreadItem::Kind::McpToolCall> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemMcpToolCall, qodex::codex::ThreadItem::Kind::McpToolCall>;

    explicit CompletedMcpToolCall(qodex::codex::ThreadItemMcpToolCall payload);
    ~CompletedMcpToolCall() override;
};

}  // namespace qodex::domain::threadmodel
