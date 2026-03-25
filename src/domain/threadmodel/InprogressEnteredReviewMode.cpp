#include "domain/threadmodel/InprogressEnteredReviewMode.h"

namespace qodex::domain::threadmodel {

InprogressEnteredReviewMode::InprogressEnteredReviewMode(qodex::codex::ThreadItemEnteredReviewMode payload)
    : Base(std::move(payload)) {
}

InprogressEnteredReviewMode::~InprogressEnteredReviewMode() = default;

}  // namespace qodex::domain::threadmodel
