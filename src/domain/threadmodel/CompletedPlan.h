#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedPlan final
    : public PayloadCompletedItem<qodex::codex::ThreadItemPlan, qodex::codex::ThreadItem::Kind::Plan> {
public:
    using Base = PayloadCompletedItem<qodex::codex::ThreadItemPlan, qodex::codex::ThreadItem::Kind::Plan>;

    explicit CompletedPlan(qodex::codex::ThreadItemPlan payload);
    ~CompletedPlan() override;
};

}  // namespace qodex::domain::threadmodel
