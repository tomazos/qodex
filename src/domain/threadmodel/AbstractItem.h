#pragma once

#include <QString>

#include "CodexProtocol.h"

namespace qodex::domain::threadmodel {

class AbstractItem {
public:
    explicit AbstractItem(QString id);
    virtual ~AbstractItem();

    [[nodiscard]] const QString &id() const;
    [[nodiscard]] virtual qodex::codex::ThreadItem::Kind kind() const = 0;
    [[nodiscard]] virtual bool isCompleted() const = 0;

private:
    QString m_id;
};

}  // namespace qodex::domain::threadmodel
