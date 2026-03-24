#include "threadui_native/ThreadUiEngine.h"

#include <iostream>
#include <utility>

namespace qodex::threadui::native {

namespace {

bool initialized = false;
std::int64_t frameCount = 0;
FrameCountDisplayCallback frameCountDisplayCallback;

bool isPowerOfTwo(const std::int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace

void initialize() {
    if (initialized) {
        return;
    }

    initialized = true;
    frameCount = 0;
    std::cout << "Qodex thread UI engine initialized." << std::endl;
}

void setFrameCountDisplayCallback(FrameCountDisplayCallback callback) {
    frameCountDisplayCallback = std::move(callback);
}

void tick() {
    if (!initialized) {
        return;
    }

    if (isPowerOfTwo(frameCount)) {
        std::cout << "Frame " << frameCount << std::endl;
    }

    if (frameCountDisplayCallback) {
        frameCountDisplayCallback(frameCount);
    }

    ++frameCount;
}

void shutdown() {
    if (!initialized) {
        return;
    }

    initialized = false;
    frameCountDisplayCallback = nullptr;
    std::cout << "Qodex thread UI engine shutdown." << std::endl;
}

}  // namespace qodex::threadui::native
