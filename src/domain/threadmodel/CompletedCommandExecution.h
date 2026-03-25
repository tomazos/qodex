#pragma once

#include "domain/threadmodel/PayloadItemBase.h"

namespace qodex::domain::threadmodel {

class CompletedCommandExecution final
    : public PayloadCompletedItem<
          qodex::codex::ThreadItemCommandExecution,
          qodex::codex::ThreadItem::Kind::CommandExecution> {
public:
    using Base = PayloadCompletedItem<
        qodex::codex::ThreadItemCommandExecution,
        qodex::codex::ThreadItem::Kind::CommandExecution>;

    explicit CompletedCommandExecution(qodex::codex::ThreadItemCommandExecution payload);
    ~CompletedCommandExecution() override;
};

}  // namespace qodex::domain::threadmodel
