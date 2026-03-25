#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressUserMessage final
    : public PayloadInprogressItem<qodex::codex::ThreadItemUserMessage, qodex::codex::ThreadItem::Kind::UserMessage> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemUserMessage, qodex::codex::ThreadItem::Kind::UserMessage>;

    explicit InprogressUserMessage(qodex::codex::ThreadItemUserMessage payload);
    ~InprogressUserMessage() override;
};

}  // namespace qodex::domain::threadmodel
