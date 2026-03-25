#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class InprogressImageGeneration final
    : public PayloadInprogressItem<
          qodex::codex::ThreadItemImageGeneration,
          qodex::codex::ThreadItem::Kind::ImageGeneration> {
public:
    using Base = PayloadInprogressItem<
        qodex::codex::ThreadItemImageGeneration,
        qodex::codex::ThreadItem::Kind::ImageGeneration>;

    explicit InprogressImageGeneration(qodex::codex::ThreadItemImageGeneration payload);
    ~InprogressImageGeneration() override;
};

}  // namespace qodex::domain::threadmodel
