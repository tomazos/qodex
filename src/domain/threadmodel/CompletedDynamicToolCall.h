#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedDynamicToolCall final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemDynamicToolCall,
          qodex::codex::ThreadItem::Kind::DynamicToolCall> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemDynamicToolCall,
        qodex::codex::ThreadItem::Kind::DynamicToolCall>;

    explicit CompletedDynamicToolCall(qodex::codex::ThreadItemDynamicToolCall payload);
    ~CompletedDynamicToolCall() override;
};

}  // namespace qodex::domain::threadmodel
