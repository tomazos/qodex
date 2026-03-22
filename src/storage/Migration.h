#pragma once

#include <QString>

namespace qodex::storage {

struct Migration {
    int version = 0;
    QString name;
    QString resourcePath;
};

}  // namespace qodex::storage
