#pragma once

#include <QString>

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressPlan final
    : public PayloadInprogressItem<qodex::codex::ThreadItemPlan, qodex::codex::ThreadItem::Kind::Plan> {
public:
    using Base = PayloadInprogressItem<qodex::codex::ThreadItemPlan, qodex::codex::ThreadItem::Kind::Plan>;

    explicit InprogressPlan(qodex::codex::ThreadItemPlan payload);
    ~InprogressPlan() override;

    void appendDelta(const QString &delta);
    [[nodiscard]] const QString &streamedText() const;
    [[nodiscard]] QJsonObject properties() const override;

private:
    QString m_streamedText;
};

}  // namespace qodex::domain::threadmodel
