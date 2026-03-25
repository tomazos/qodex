#include "domain/threadmodel/InprogressUserMessage.h"

namespace qodex::domain::threadmodel {

InprogressUserMessage::InprogressUserMessage(qodex::codex::ThreadItemUserMessage payload)
    : Base(std::move(payload)) {
}

InprogressUserMessage::~InprogressUserMessage() = default;

}  // namespace qodex::domain::threadmodel
