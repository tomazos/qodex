#include "domain/threadmodel/CompletedFileChange.h"

namespace qodex::domain::threadmodel {

CompletedFileChange::CompletedFileChange(qodex::codex::ThreadItemFileChange payload)
    : Base(std::move(payload)) {
}

CompletedFileChange::~CompletedFileChange() = default;

}  // namespace qodex::domain::threadmodel
