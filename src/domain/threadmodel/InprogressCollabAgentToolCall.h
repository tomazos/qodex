#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressCollabAgentToolCall final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemCollabAgentToolCall,
          qodex::codex::ThreadItem::Kind::CollabAgentToolCall> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemCollabAgentToolCall,
        qodex::codex::ThreadItem::Kind::CollabAgentToolCall>;

    explicit InprogressCollabAgentToolCall(qodex::codex::ThreadItemCollabAgentToolCall payload);
    ~InprogressCollabAgentToolCall() override;
};

}  // namespace qodex::domain::threadmodel
