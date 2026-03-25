#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedContextCompaction final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemContextCompaction,
          qodex::codex::ThreadItem::Kind::ContextCompaction> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemContextCompaction,
        qodex::codex::ThreadItem::Kind::ContextCompaction>;

    explicit CompletedContextCompaction(qodex::codex::ThreadItemContextCompaction payload);
    ~CompletedContextCompaction() override;
};

}  // namespace qodex::domain::threadmodel
