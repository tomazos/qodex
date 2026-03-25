#pragma once

#include <QJsonObject>
#include <QJsonValue>

#include <utility>

#include "domain/threadmodel/CompletedItem.h"
#include "domain/threadmodel/InprogressItem.h"

namespace qodex::domain::threadmodel {

template <typename BaseT, typename PayloadT, qodex::codex::ThreadItem::Kind KindValue>
class PayloadItemBase : public BaseT {
public:
    explicit PayloadItemBase(PayloadT payload)
        : BaseT(payload.id),
          m_payload(std::move(payload)) {
    }

    [[nodiscard]] qodex::codex::ThreadItem::Kind kind() const override {
        return KindValue;
    }

    [[nodiscard]] QJsonObject properties() const override {
        const QJsonValue value = qodex::codex::toJson(m_payload);
        if (value.isObject()) {
            return value.toObject();
        }

        QJsonObject properties;
        properties.insert(QStringLiteral("value"), value);
        return properties;
    }

    [[nodiscard]] const PayloadT &data() const {
        return m_payload;
    }

protected:
    [[nodiscard]] PayloadT &mutableData() {
        return m_payload;
    }

private:
    PayloadT m_payload;
};

template <typename PayloadT, qodex::codex::ThreadItem::Kind KindValue>
using PayloadCompletedItem = PayloadItemBase<CompletedItem, PayloadT, KindValue>;

template <typename PayloadT, qodex::codex::ThreadItem::Kind KindValue>
using PayloadInprogressItem = PayloadItemBase<InprogressItem, PayloadT, KindValue>;

}  // namespace qodex::domain::threadmodel
