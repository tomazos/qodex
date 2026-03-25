#include "domain/threadmodel/InprogressImageGeneration.h"

namespace qodex::domain::threadmodel {

InprogressImageGeneration::InprogressImageGeneration(qodex::codex::ThreadItemImageGeneration payload)
    : Base(std::move(payload)) {
}

InprogressImageGeneration::~InprogressImageGeneration() = default;

}  // namespace qodex::domain::threadmodel
