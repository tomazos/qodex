#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedEnteredReviewMode final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemEnteredReviewMode,
          qodex::codex::ThreadItem::Kind::EnteredReviewMode> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemEnteredReviewMode,
        qodex::codex::ThreadItem::Kind::EnteredReviewMode>;

    explicit CompletedEnteredReviewMode(qodex::codex::ThreadItemEnteredReviewMode payload);
    ~CompletedEnteredReviewMode() override;
};

}  // namespace qodex::domain::threadmodel
