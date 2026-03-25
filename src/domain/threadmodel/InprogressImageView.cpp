#include "domain/threadmodel/InprogressImageView.h"

namespace qodex::domain::threadmodel {

InprogressImageView::InprogressImageView(qodex::codex::ThreadItemImageView payload)
    : Base(std::move(payload)) {
}

InprogressImageView::~InprogressImageView() = default;

}  // namespace qodex::domain::threadmodel
