#pragma once

#include "domain/threadmodel/AbstractItem.h"

namespace qodex::domain::threadmodel {

class CompletedItem : public AbstractItem {
public:
    explicit CompletedItem(QString id);
    ~CompletedItem() override;

    [[nodiscard]] bool isCompleted() const override;
};

}  // namespace qodex::domain::threadmodel
