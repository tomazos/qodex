#include "domain/threadmodel/InprogressItem.h"

namespace qodex::domain::threadmodel {

InprogressItem::InprogressItem(QString id)
    : AbstractItem(std::move(id)) {
}

InprogressItem::~InprogressItem() = default;

bool InprogressItem::isCompleted() const {
    return false;
}

}  // namespace qodex::domain::threadmodel
