#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressContextCompaction final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemContextCompaction,
          qodex::codex::ThreadItem::Kind::ContextCompaction> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemContextCompaction,
        qodex::codex::ThreadItem::Kind::ContextCompaction>;

    explicit InprogressContextCompaction(qodex::codex::ThreadItemContextCompaction payload);
    ~InprogressContextCompaction() override;
};

}  // namespace qodex::domain::threadmodel
