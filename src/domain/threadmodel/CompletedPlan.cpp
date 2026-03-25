#include "domain/threadmodel/CompletedPlan.h"

namespace qodex::domain::threadmodel {

CompletedPlan::CompletedPlan(qodex::codex::ThreadItemPlan payload)
    : Base(std::move(payload)) {
}

CompletedPlan::~CompletedPlan() = default;

}  // namespace qodex::domain::threadmodel
