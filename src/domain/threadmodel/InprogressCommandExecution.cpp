#include "domain/threadmodel/InprogressCommandExecution.h"

namespace qodex::domain::threadmodel {

InprogressCommandExecution::InprogressCommandExecution(qodex::codex::ThreadItemCommandExecution payload)
    : Base(std::move(payload)) {
}

InprogressCommandExecution::~InprogressCommandExecution() = default;

void InprogressCommandExecution::appendOutputDelta(const QString &delta) {
    if (mutableData().aggregatedOutput.hasValue()) {
        mutableData().aggregatedOutput.value() += delta;
        return;
    }

    mutableData().aggregatedOutput = qodex::codex::Nullable<QString>::fromValue(delta);
}

void InprogressCommandExecution::recordTerminalInteraction(const QString &processId, const QString &stdin) {
    mutableData().processId = qodex::codex::Nullable<QString>::fromValue(processId);
    m_terminalInputs.append(stdin);
}

const QStringList &InprogressCommandExecution::terminalInputs() const {
    return m_terminalInputs;
}

QJsonObject InprogressCommandExecution::properties() const {
    QJsonObject properties = Base::properties();

    QJsonArray terminalInputsJson;
    for (const QString &input : m_terminalInputs) {
        terminalInputsJson.append(input);
    }
    properties.insert(QStringLiteral("terminalInputs"), terminalInputsJson);
    return properties;
}

}  // namespace qodex::domain::threadmodel
