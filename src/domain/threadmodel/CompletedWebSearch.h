#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedWebSearch final
    : public PayloadCompletedItem<qodex::codex::ThreadItemWebSearch, qodex::codex::ThreadItem::Kind::WebSearch> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemWebSearch, qodex::codex::ThreadItem::Kind::WebSearch>;

    explicit CompletedWebSearch(qodex::codex::ThreadItemWebSearch payload);
    ~CompletedWebSearch() override;
};

}  // namespace qodex::domain::threadmodel
