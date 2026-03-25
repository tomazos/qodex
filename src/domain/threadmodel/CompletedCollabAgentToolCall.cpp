#include "domain/threadmodel/CompletedCollabAgentToolCall.h"

namespace qodex::domain::threadmodel {

CompletedCollabAgentToolCall::CompletedCollabAgentToolCall(qodex::codex::ThreadItemCollabAgentToolCall payload)
    : Base(std::move(payload)) {
}

CompletedCollabAgentToolCall::~CompletedCollabAgentToolCall() = default;

}  // namespace qodex::domain::threadmodel
