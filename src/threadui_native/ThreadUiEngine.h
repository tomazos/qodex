#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qodex::threadui::native {

using FrameCountDisplayCallback = std::function<void(std::int64_t)>;

enum class DisplayItemKind {
    UserMessage,
    AgentMessage,
};

struct DisplayItem {
    DisplayItemKind kind = DisplayItemKind::UserMessage;
    std::string text;
};

struct LaunchConfig {
    std::string host;
    std::uint16_t port = 0;
    std::string token;
};

void initialize(const LaunchConfig &launchConfig = {});
void tick();
void shutdown();
void setFrameCountDisplayCallback(FrameCountDisplayCallback callback);
[[nodiscard]] std::vector<DisplayItem> takePendingItems();

}  // namespace qodex::threadui::native
