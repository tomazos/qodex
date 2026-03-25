#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressWebSearch final
    : public PayloadInprogressItem<qodex::codex::ThreadItemWebSearch, qodex::codex::ThreadItem::Kind::WebSearch> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemWebSearch, qodex::codex::ThreadItem::Kind::WebSearch>;

    explicit InprogressWebSearch(qodex::codex::ThreadItemWebSearch payload);
    ~InprogressWebSearch() override;
};

}  // namespace qodex::domain::threadmodel
