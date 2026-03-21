#pragma once

#include <QString>

namespace qodex::domain {

struct ThreadSummary {
    QString id;
    QString title;
    QString preview;
    QString cwd;
    QString statusText;
    bool archived = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

}  // namespace qodex::domain
