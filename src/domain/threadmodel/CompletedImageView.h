#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedImageView final
    : public PayloadCompletedItem<qodex::codex::ThreadItemImageView, qodex::codex::ThreadItem::Kind::ImageView> {
public:
    using Base =
        PayloadCompletedItem<qodex::codex::ThreadItemImageView, qodex::codex::ThreadItem::Kind::ImageView>;

    explicit CompletedImageView(qodex::codex::ThreadItemImageView payload);
    ~CompletedImageView() override;
};

}  // namespace qodex::domain::threadmodel
