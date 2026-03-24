#pragma once

#include <cstdint>
#include <functional>

namespace qodex::threadui::native {

using FrameCountDisplayCallback = std::function<void(std::int64_t)>;

void initialize();
void tick();
void shutdown();
void setFrameCountDisplayCallback(FrameCountDisplayCallback callback);

}  // namespace qodex::threadui::native
