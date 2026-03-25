#include "domain/threadmodel/CompletedUserMessage.h"

namespace qodex::domain::threadmodel {

CompletedUserMessage::CompletedUserMessage(qodex::codex::ThreadItemUserMessage payload)
    : Base(std::move(payload)) {
}

CompletedUserMessage::~CompletedUserMessage() = default;

}  // namespace qodex::domain::threadmodel
