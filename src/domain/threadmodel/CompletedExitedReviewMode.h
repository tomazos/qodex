#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedExitedReviewMode final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemExitedReviewMode,
          qodex::codex::ThreadItem::Kind::ExitedReviewMode> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemExitedReviewMode,
        qodex::codex::ThreadItem::Kind::ExitedReviewMode>;

    explicit CompletedExitedReviewMode(qodex::codex::ThreadItemExitedReviewMode payload);
    ~CompletedExitedReviewMode() override;
};

}  // namespace qodex::domain::threadmodel
