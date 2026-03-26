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

struct DisplayCommandExecution {
    std::string command;
    std::string cwd;
    std::string status;
    bool hasExitCode = false;
    std::int64_t exitCode = 0;
    bool hasDurationMs = false;
    std::int64_t durationMs = 0;
    std::string processId;
    std::string aggregatedOutput;
    std::vector<std::string> actionLabels;
};

struct DisplayItem {
    std::string id;
    std::string kind;
    std::string text;
    DisplayCommandExecution commandExecution;
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
