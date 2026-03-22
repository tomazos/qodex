#pragma once

#include <QString>

namespace qodex::codex {
struct Thread;
}

namespace qodex::ui {

[[nodiscard]] QString formatThreadTranscriptHtml(const qodex::codex::Thread &thread);

}  // namespace qodex::ui
