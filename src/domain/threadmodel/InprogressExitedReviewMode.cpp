#include "domain/threadmodel/InprogressExitedReviewMode.h"

namespace qodex::domain::threadmodel {

InprogressExitedReviewMode::InprogressExitedReviewMode(qodex::codex::ThreadItemExitedReviewMode payload)
    : Base(std::move(payload)) {
}

InprogressExitedReviewMode::~InprogressExitedReviewMode() = default;

}  // namespace qodex::domain::threadmodel
