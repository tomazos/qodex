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

QJsonObject InprogressPlan::properties() const {
    QJsonObject properties = Base::properties();
    properties.insert(QStringLiteral("streamedText"), m_streamedText);
    return properties;
}

}  // namespace qodex::domain::threadmodel
