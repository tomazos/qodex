#pragma once

#include <QStringList>

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressCommandExecution final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemCommandExecution,
          qodex::codex::ThreadItem::Kind::CommandExecution> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemCommandExecution,
        qodex::codex::ThreadItem::Kind::CommandExecution>;

    explicit InprogressCommandExecution(qodex::codex::ThreadItemCommandExecution payload);
    ~InprogressCommandExecution() override;

    void appendOutputDelta(const QString &delta);
    void recordTerminalInteraction(const QString &processId, const QString &stdin);
    [[nodiscard]] const QStringList &terminalInputs() const;
    [[nodiscard]] QJsonObject properties() const override;

private:
    QStringList m_terminalInputs;
};

}  // namespace qodex::domain::threadmodel
