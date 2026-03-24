#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace qodex::threadui::native {

using FrameCountDisplayCallback = std::function<void(std::int64_t)>;

struct LaunchConfig {
    std::string host;
    std::uint16_t port = 0;
    std::string token;
};

void initialize(const LaunchConfig &launchConfig = {});
void tick();
void shutdown();
void setFrameCountDisplayCallback(FrameCountDisplayCallback callback);

}  // namespace qodex::threadui::native
