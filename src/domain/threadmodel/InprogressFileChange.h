#pragma once

#include <QString>

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressFileChange final
    : public PayloadInprogressItem<qodex::codex::ThreadItemFileChange, qodex::codex::ThreadItem::Kind::FileChange> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemFileChange, qodex::codex::ThreadItem::Kind::FileChange>;

    explicit InprogressFileChange(qodex::codex::ThreadItemFileChange payload);
    ~InprogressFileChange() override;

    void appendOutputDelta(const QString &delta);
    [[nodiscard]] const QString &outputDelta() const;

private:
    QString m_outputDelta;
};

}  // namespace qodex::domain::threadmodel
