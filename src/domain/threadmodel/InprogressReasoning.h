#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressReasoning final
    : public PayloadInprogressItem<qodex::codex::ThreadItemReasoning, qodex::codex::ThreadItem::Kind::Reasoning> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemReasoning, qodex::codex::ThreadItem::Kind::Reasoning>;

    explicit InprogressReasoning(qodex::codex::ThreadItemReasoning payload);
    ~InprogressReasoning() override;

    void appendContentDelta(qint64 contentIndex, const QString &delta);
    void addSummaryPart(qint64 summaryIndex);
    void appendSummaryTextDelta(qint64 summaryIndex, const QString &delta);
};

}  // namespace qodex::domain::threadmodel
