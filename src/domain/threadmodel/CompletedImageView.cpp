#include "domain/threadmodel/CompletedImageView.h"

namespace qodex::domain::threadmodel {

CompletedImageView::CompletedImageView(qodex::codex::ThreadItemImageView payload)
    : Base(std::move(payload)) {
}

CompletedImageView::~CompletedImageView() = default;

}  // namespace qodex::domain::threadmodel
