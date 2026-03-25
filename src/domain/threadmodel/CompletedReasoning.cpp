#include "domain/threadmodel/CompletedReasoning.h"

namespace qodex::domain::threadmodel {

CompletedReasoning::CompletedReasoning(qodex::codex::ThreadItemReasoning payload)
    : Base(std::move(payload)) {
}

CompletedReasoning::~CompletedReasoning() = default;

}  // namespace qodex::domain::threadmodel
