#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedFileChange final
    : public PayloadCompletedItem<qodex::codex::ThreadItemFileChange, qodex::codex::ThreadItem::Kind::FileChange> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemFileChange, qodex::codex::ThreadItem::Kind::FileChange>;

    explicit CompletedFileChange(qodex::codex::ThreadItemFileChange payload);
    ~CompletedFileChange() override;
};

}  // namespace qodex::domain::threadmodel
