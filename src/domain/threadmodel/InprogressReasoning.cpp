#include "domain/threadmodel/InprogressReasoning.h"

namespace qodex::domain::threadmodel {

namespace {

void ensureStringIndex(std::optional<QList<QString>> &strings, const qint64 index) {
    if (index < 0) {
        return;
    }

    if (!strings.has_value()) {
        strings = QList<QString>{};
    }

    while (strings->size() <= index) {
        strings->append(QString{});
    }
}

}  // namespace

InprogressReasoning::InprogressReasoning(qodex::codex::ThreadItemReasoning payload)
    : Base(std::move(payload)) {
}

InprogressReasoning::~InprogressReasoning() = default;

void InprogressReasoning::appendContentDelta(const qint64 contentIndex, const QString &delta) {
    ensureStringIndex(mutableData().content, contentIndex);
    if (mutableData().content.has_value() && contentIndex >= 0 && mutableData().content->size() > contentIndex) {
        (*mutableData().content)[contentIndex] += delta;
    }
}

void InprogressReasoning::addSummaryPart(const qint64 summaryIndex) {
    ensureStringIndex(mutableData().summary, summaryIndex);
}

void InprogressReasoning::appendSummaryTextDelta(const qint64 summaryIndex, const QString &delta) {
    ensureStringIndex(mutableData().summary, summaryIndex);
    if (mutableData().summary.has_value() && summaryIndex >= 0 && mutableData().summary->size() > summaryIndex) {
        (*mutableData().summary)[summaryIndex] += delta;
    }
}

}  // namespace qodex::domain::threadmodel
