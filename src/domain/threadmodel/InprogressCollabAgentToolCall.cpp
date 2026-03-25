#include "domain/threadmodel/InprogressCollabAgentToolCall.h"

namespace qodex::domain::threadmodel {

InprogressCollabAgentToolCall::InprogressCollabAgentToolCall(qodex::codex::ThreadItemCollabAgentToolCall payload)
    : Base(std::move(payload)) {
}

InprogressCollabAgentToolCall::~InprogressCollabAgentToolCall() = default;

}  // namespace qodex::domain::threadmodel
