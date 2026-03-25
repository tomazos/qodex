#include "domain/threadmodel/InprogressFileChange.h"

namespace qodex::domain::threadmodel {

InprogressFileChange::InprogressFileChange(qodex::codex::ThreadItemFileChange payload)
    : Base(std::move(payload)) {
}

InprogressFileChange::~InprogressFileChange() = default;

void InprogressFileChange::appendOutputDelta(const QString &delta) {
    m_outputDelta += delta;
}

const QString &InprogressFileChange::outputDelta() const {
    return m_outputDelta;
}

}  // namespace qodex::domain::threadmodel
