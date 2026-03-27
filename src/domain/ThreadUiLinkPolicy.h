#pragma once

#include <QString>

#include "common.pb.h"

namespace qodex::domain {

class ThreadUiLinkPolicy {
public:
    [[nodiscard]] qodex::threadui::ipc::common::ResolvedLink resolveLink(
        const QString &rawHref,
        const QString &cwd
    ) const;
};

}  // namespace qodex::domain
