#pragma once

#include <QString>
#include <QUrl>

namespace qodex::codex {
struct Thread;
}

namespace qodex::ui {

[[nodiscard]] QString formatThreadTranscriptHtml(const qodex::codex::Thread &thread, const QUrl &baseUrl);

}  // namespace qodex::ui
