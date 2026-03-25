#include "domain/threadmodel/CompletedCommandExecution.h"

namespace qodex::domain::threadmodel {

CompletedCommandExecution::CompletedCommandExecution(qodex::codex::ThreadItemCommandExecution payload)
    : Base(std::move(payload)) {
}

CompletedCommandExecution::~CompletedCommandExecution() = default;

}  // namespace qodex::domain::threadmodel
