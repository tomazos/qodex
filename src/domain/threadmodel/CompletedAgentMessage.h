#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedAgentMessage final
    : public PayloadCompletedItem<qodex::codex::ThreadItemAgentMessage, qodex::codex::ThreadItem::Kind::AgentMessage> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemAgentMessage, qodex::codex::ThreadItem::Kind::AgentMessage>;

    explicit CompletedAgentMessage(qodex::codex::ThreadItemAgentMessage payload);
    ~CompletedAgentMessage() override;
};

}  // namespace qodex::domain::threadmodel
