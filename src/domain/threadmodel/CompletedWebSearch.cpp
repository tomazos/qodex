#include "domain/threadmodel/CompletedWebSearch.h"

namespace qodex::domain::threadmodel {

CompletedWebSearch::CompletedWebSearch(qodex::codex::ThreadItemWebSearch payload)
    : Base(std::move(payload)) {
}

CompletedWebSearch::~CompletedWebSearch() = default;

}  // namespace qodex::domain::threadmodel
