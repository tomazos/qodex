#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressImageView final
    : public PayloadInprogressItem<qodex::codex::ThreadItemImageView, qodex::codex::ThreadItem::Kind::ImageView> {
public:
    using Base =
        PayloadInprogressItem<qodex::codex::ThreadItemImageView, qodex::codex::ThreadItem::Kind::ImageView>;

    explicit InprogressImageView(qodex::codex::ThreadItemImageView payload);
    ~InprogressImageView() override;
};

}  // namespace qodex::domain::threadmodel
