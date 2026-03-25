#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedCollabAgentToolCall final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemCollabAgentToolCall,
          qodex::codex::ThreadItem::Kind::CollabAgentToolCall> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemCollabAgentToolCall,
        qodex::codex::ThreadItem::Kind::CollabAgentToolCall>;

    explicit CompletedCollabAgentToolCall(qodex::codex::ThreadItemCollabAgentToolCall payload);
    ~CompletedCollabAgentToolCall() override;
};

}  // namespace qodex::domain::threadmodel
