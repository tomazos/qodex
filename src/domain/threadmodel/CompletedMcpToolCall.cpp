#include "domain/threadmodel/CompletedMcpToolCall.h"

namespace qodex::domain::threadmodel {

CompletedMcpToolCall::CompletedMcpToolCall(qodex::codex::ThreadItemMcpToolCall payload)
    : Base(std::move(payload)) {
}

CompletedMcpToolCall::~CompletedMcpToolCall() = default;

}  // namespace qodex::domain::threadmodel
