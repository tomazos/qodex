#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedImageGeneration final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemImageGeneration,
          qodex::codex::ThreadItem::Kind::ImageGeneration> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemImageGeneration,
        qodex::codex::ThreadItem::Kind::ImageGeneration>;

    explicit CompletedImageGeneration(qodex::codex::ThreadItemImageGeneration payload);
    ~CompletedImageGeneration() override;
};

}  // namespace qodex::domain::threadmodel
