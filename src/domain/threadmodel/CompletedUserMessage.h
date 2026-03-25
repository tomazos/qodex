#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedUserMessage final
    : public PayloadCompletedItem<qodex::codex::ThreadItemUserMessage, qodex::codex::ThreadItem::Kind::UserMessage> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemUserMessage, qodex::codex::ThreadItem::Kind::UserMessage>;

    explicit CompletedUserMessage(qodex::codex::ThreadItemUserMessage payload);
    ~CompletedUserMessage() override;
};

}  // namespace qodex::domain::threadmodel
