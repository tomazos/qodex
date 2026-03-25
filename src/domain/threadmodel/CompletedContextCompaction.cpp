#include "domain/threadmodel/CompletedContextCompaction.h"

namespace qodex::domain::threadmodel {

CompletedContextCompaction::CompletedContextCompaction(qodex::codex::ThreadItemContextCompaction payload)
    : Base(std::move(payload)) {
}

CompletedContextCompaction::~CompletedContextCompaction() = default;

}  // namespace qodex::domain::threadmodel
