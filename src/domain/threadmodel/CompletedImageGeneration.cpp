#include "domain/threadmodel/CompletedImageGeneration.h"

namespace qodex::domain::threadmodel {

CompletedImageGeneration::CompletedImageGeneration(qodex::codex::ThreadItemImageGeneration payload)
    : Base(std::move(payload)) {
}

CompletedImageGeneration::~CompletedImageGeneration() = default;

}  // namespace qodex::domain::threadmodel
