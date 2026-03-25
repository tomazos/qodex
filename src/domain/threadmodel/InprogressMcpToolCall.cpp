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

QJsonObject InprogressMcpToolCall::properties() const {
    QJsonObject properties = Base::properties();

    QJsonArray progressMessagesJson;
    for (const QString &message : m_progressMessages) {
        progressMessagesJson.append(message);
    }
    properties.insert(QStringLiteral("progressMessages"), progressMessagesJson);
    return properties;
}

}  // namespace qodex::domain::threadmodel
