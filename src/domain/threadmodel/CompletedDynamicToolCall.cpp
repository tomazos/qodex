#include "domain/threadmodel/CompletedDynamicToolCall.h"

namespace qodex::domain::threadmodel {

CompletedDynamicToolCall::CompletedDynamicToolCall(qodex::codex::ThreadItemDynamicToolCall payload)
    : Base(std::move(payload)) {
}

CompletedDynamicToolCall::~CompletedDynamicToolCall() = default;

}  // namespace qodex::domain::threadmodel
