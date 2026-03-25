#include "domain/threadmodel/InprogressWebSearch.h"

namespace qodex::domain::threadmodel {

InprogressWebSearch::InprogressWebSearch(qodex::codex::ThreadItemWebSearch payload)
    : Base(std::move(payload)) {
}

InprogressWebSearch::~InprogressWebSearch() = default;

}  // namespace qodex::domain::threadmodel
