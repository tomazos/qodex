#include "domain/threadmodel/InprogressMcpToolCall.h"

namespace qodex::domain::threadmodel {

InprogressMcpToolCall::InprogressMcpToolCall(qodex::codex::ThreadItemMcpToolCall payload)
    : Base(std::move(payload)) {
}

InprogressMcpToolCall::~InprogressMcpToolCall() = default;

void InprogressMcpToolCall::appendProgressMessage(const QString &message) {
    m_progressMessages.append(message);
}

const QStringList &InprogressMcpToolCall::progressMessages() const {
    return m_progressMessages;
}

}  // namespace qodex::domain::threadmodel
