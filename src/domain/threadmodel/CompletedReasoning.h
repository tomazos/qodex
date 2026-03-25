#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedReasoning final
    : public PayloadCompletedItem<qodex::codex::ThreadItemReasoning, qodex::codex::ThreadItem::Kind::Reasoning> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemReasoning, qodex::codex::ThreadItem::Kind::Reasoning>;

    explicit CompletedReasoning(qodex::codex::ThreadItemReasoning payload);
    ~CompletedReasoning() override;
};

}  // namespace qodex::domain::threadmodel
