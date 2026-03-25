#include "domain/threadmodel/InprogressPlan.h"

namespace qodex::domain::threadmodel {

InprogressPlan::InprogressPlan(qodex::codex::ThreadItemPlan payload)
    : Base(std::move(payload)),
      m_streamedText(data().text) {
}

InprogressPlan::~InprogressPlan() = default;

void InprogressPlan::appendDelta(const QString &delta) {
    m_streamedText += delta;
}

const QString &InprogressPlan::streamedText() const {
    return m_streamedText;
}

}  // namespace qodex::domain::threadmodel
