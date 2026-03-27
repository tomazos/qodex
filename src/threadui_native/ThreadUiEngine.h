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

struct ResolvedLink {
    std::uint64_t requestId = 0;
    bool ok = false;
    std::string message;
    std::string rawHref;
    std::string normalizedHref;
    std::string tooltip;
    std::string kind;
    std::string resolvedPath;
    bool exists = false;
    bool isDirectory = false;
    bool hasLine = false;
    std::int64_t line = 0;
    bool hasColumn = false;
    std::int64_t column = 0;
    std::string defaultAction;
    bool canOpen = false;
    bool canOpenExternally = false;
    bool canRevealInFolder = false;
    bool canCopyResolvedPath = false;
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
[[nodiscard]] std::uint64_t resolveLink(const std::string &href);
void setFrameCountDisplayCallback(FrameCountDisplayCallback callback);
[[nodiscard]] std::vector<DisplayItem> takePendingItems();
[[nodiscard]] std::vector<ResolvedLink> takePendingResolvedLinks();
[[nodiscard]] std::string takeFatalError();
[[nodiscard]] std::string takePendingError();

}  // namespace qodex::threadui::native
