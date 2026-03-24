#pragma once

#include <cstdint>
#include <string>

#include "common.pb.h"

namespace qodex::threadui::ipc {

enum class FrameDecodeResult {
    Incomplete,
    Success,
    InvalidFrame,
};

inline constexpr std::size_t kMaxEnvelopeLengthPrefixBytes = 5;
inline constexpr std::uint32_t kMaxEnvelopePayloadSizeBytes = 1024U * 1024U;
inline constexpr char kLoginMethodName[] = "Login";

[[nodiscard]] std::string encodeEnvelopeFrame(const qodex::threadui::ipc::common::RpcEnvelope &envelope);
[[nodiscard]] bool parseEnvelopePayload(
    const std::string &payload,
    qodex::threadui::ipc::common::RpcEnvelope *envelope,
    std::string *errorMessage = nullptr
);
[[nodiscard]] FrameDecodeResult tryDecodeNextEnvelope(
    std::string *buffer,
    qodex::threadui::ipc::common::RpcEnvelope *envelope,
    std::string *errorMessage = nullptr
);

}  // namespace qodex::threadui::ipc
