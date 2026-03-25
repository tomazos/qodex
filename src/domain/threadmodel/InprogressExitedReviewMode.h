#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressExitedReviewMode final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemExitedReviewMode,
          qodex::codex::ThreadItem::Kind::ExitedReviewMode> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemExitedReviewMode,
        qodex::codex::ThreadItem::Kind::ExitedReviewMode>;

    explicit InprogressExitedReviewMode(qodex::codex::ThreadItemExitedReviewMode payload);
    ~InprogressExitedReviewMode() override;
};

}  // namespace qodex::domain::threadmodel
