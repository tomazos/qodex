#pragma once

#include <QStringList>

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressMcpToolCall final
    : public PayloadInprogressItem<qodex::codex::ThreadItemMcpToolCall, qodex::codex::ThreadItem::Kind::McpToolCall> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemMcpToolCall, qodex::codex::ThreadItem::Kind::McpToolCall>;

    explicit InprogressMcpToolCall(qodex::codex::ThreadItemMcpToolCall payload);
    ~InprogressMcpToolCall() override;

    void appendProgressMessage(const QString &message);
    [[nodiscard]] const QStringList &progressMessages() const;
    [[nodiscard]] QJsonObject properties() const override;

private:
    QStringList m_progressMessages;
};

}  // namespace qodex::domain::threadmodel
