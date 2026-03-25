#include "domain/threadmodel/CompletedEnteredReviewMode.h"

namespace qodex::domain::threadmodel {

CompletedEnteredReviewMode::CompletedEnteredReviewMode(qodex::codex::ThreadItemEnteredReviewMode payload)
    : Base(std::move(payload)) {
}

CompletedEnteredReviewMode::~CompletedEnteredReviewMode() = default;

}  // namespace qodex::domain::threadmodel
