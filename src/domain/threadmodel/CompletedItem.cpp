#include "domain/threadmodel/CompletedItem.h"

namespace qodex::domain::threadmodel {

CompletedItem::CompletedItem(QString id)
    : AbstractItem(std::move(id)) {
}

CompletedItem::~CompletedItem() = default;

bool CompletedItem::isCompleted() const {
    return true;
}

}  // namespace qodex::domain::threadmodel
