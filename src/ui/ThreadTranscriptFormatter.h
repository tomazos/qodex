#pragma once

#include <QString>

namespace qodex::codex {
struct Thread;
}

namespace qodex::ui {

[[nodiscard]] QString formatThreadTranscript(const qodex::codex::Thread &thread);

}  // namespace qodex::ui
