#include "domain/threadmodel/CompletedExitedReviewMode.h"

namespace qodex::domain::threadmodel {

CompletedExitedReviewMode::CompletedExitedReviewMode(qodex::codex::ThreadItemExitedReviewMode payload)
    : Base(std::move(payload)) {
}

CompletedExitedReviewMode::~CompletedExitedReviewMode() = default;

}  // namespace qodex::domain::threadmodel
