#pragma once

#include "domain/threadmodel/AbstractItem.h"

namespace qodex::domain::threadmodel {

class InprogressItem : public AbstractItem {
public:
    explicit InprogressItem(QString id);
    ~InprogressItem() override;

    [[nodiscard]] bool isCompleted() const override;
};

}  // namespace qodex::domain::threadmodel
