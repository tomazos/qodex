#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressAgentMessage final
    : public PayloadInprogressItem<qodex::codex::ThreadItemAgentMessage, qodex::codex::ThreadItem::Kind::AgentMessage> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemAgentMessage, qodex::codex::ThreadItem::Kind::AgentMessage>;

    explicit InprogressAgentMessage(qodex::codex::ThreadItemAgentMessage payload);
    ~InprogressAgentMessage() override;

    void appendDelta(const QString &delta);
};

}  // namespace qodex::domain::threadmodel
