#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressEnteredReviewMode final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemEnteredReviewMode,
          qodex::codex::ThreadItem::Kind::EnteredReviewMode> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemEnteredReviewMode,
        qodex::codex::ThreadItem::Kind::EnteredReviewMode>;

    explicit InprogressEnteredReviewMode(qodex::codex::ThreadItemEnteredReviewMode payload);
    ~InprogressEnteredReviewMode() override;
};

}  // namespace qodex::domain::threadmodel
