#include "domain/threadmodel/AbstractItem.h"

namespace qodex::domain::threadmodel {

AbstractItem::AbstractItem(QString id)
    : m_id(std::move(id)) {
}

AbstractItem::~AbstractItem() = default;

const QString &AbstractItem::id() const {
    return m_id;
}

}  // namespace qodex::domain::threadmodel
