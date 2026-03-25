#include "domain/threadmodel/InprogressContextCompaction.h"

namespace qodex::domain::threadmodel {

InprogressContextCompaction::InprogressContextCompaction(qodex::codex::ThreadItemContextCompaction payload)
    : Base(std::move(payload)) {
}

InprogressContextCompaction::~InprogressContextCompaction() = default;

}  // namespace qodex::domain::threadmodel
