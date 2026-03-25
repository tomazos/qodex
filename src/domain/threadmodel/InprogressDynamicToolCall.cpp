#include "domain/threadmodel/InprogressDynamicToolCall.h"

namespace qodex::domain::threadmodel {

InprogressDynamicToolCall::InprogressDynamicToolCall(qodex::codex::ThreadItemDynamicToolCall payload)
    : Base(std::move(payload)) {
}

InprogressDynamicToolCall::~InprogressDynamicToolCall() = default;

}  // namespace qodex::domain::threadmodel
