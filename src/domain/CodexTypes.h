#pragma once

#include <QString>

namespace qodex::domain {

struct ThreadSummary {
    QString id;
    QString title;
    QString preview;
    QString cwd;
    QString statusText;
    QString sourceText;
    QString modelProvider;
    QString cliVersion;
    QString path;
    QString agentNickname;
    QString agentRole;
    QString gitOrigin;
    QString gitBranch;
    QString gitSha;
    bool archived = false;
    bool ephemeral = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

}  // namespace qodex::domain
