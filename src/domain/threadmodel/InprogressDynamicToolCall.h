#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressDynamicToolCall final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemDynamicToolCall,
          qodex::codex::ThreadItem::Kind::DynamicToolCall> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemDynamicToolCall,
        qodex::codex::ThreadItem::Kind::DynamicToolCall>;

    explicit InprogressDynamicToolCall(qodex::codex::ThreadItemDynamicToolCall payload);
    ~InprogressDynamicToolCall() override;
};

}  // namespace qodex::domain::threadmodel
