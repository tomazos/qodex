#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qodex::threadui::native {

using FrameCountDisplayCallback = std::function<void(std::int64_t)>;

struct DisplayFileChangeChange {
    std::string path;
    std::string kind;
    std::string movePath;
    std::string diff;
};

struct DisplayFileChange {
    std::string status;
    std::vector<DisplayFileChangeChange> changes;
};

struct DisplayItem {
    std::string kind;
    std::string text;
    DisplayFileChange fileChange;
};

struct LaunchConfig {
    std::string host;
    std::uint16_t port = 0;
    std::string token;
};

void initialize(const LaunchConfig &launchConfig = {});
void tick();
void shutdown();
void sendUserInput(const std::string &text);
void setFrameCountDisplayCallback(FrameCountDisplayCallback callback);
[[nodiscard]] std::vector<DisplayItem> takePendingItems();
[[nodiscard]] std::string takeFatalError();
[[nodiscard]] std::string takePendingError();

}  // namespace qodex::threadui::native
