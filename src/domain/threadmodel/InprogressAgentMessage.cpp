#include "domain/threadmodel/InprogressAgentMessage.h"

namespace qodex::domain::threadmodel {

InprogressAgentMessage::InprogressAgentMessage(qodex::codex::ThreadItemAgentMessage payload)
    : Base(std::move(payload)) {
}

InprogressAgentMessage::~InprogressAgentMessage() = default;

void InprogressAgentMessage::appendDelta(const QString &delta) {
    mutableData().text += delta;
}

}  // namespace qodex::domain::threadmodel
