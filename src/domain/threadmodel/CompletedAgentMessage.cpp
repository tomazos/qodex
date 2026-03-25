#include "domain/threadmodel/CompletedAgentMessage.h"

namespace qodex::domain::threadmodel {

CompletedAgentMessage::CompletedAgentMessage(qodex::codex::ThreadItemAgentMessage payload)
    : Base(std::move(payload)) {
}

CompletedAgentMessage::~CompletedAgentMessage() = default;

}  // namespace qodex::domain::threadmodel
