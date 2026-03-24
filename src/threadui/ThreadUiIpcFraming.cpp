#include "threadui/ThreadUiIpcFraming.h"

#include <cstddef>

namespace qodex::threadui::ipc {

namespace {

void writeBigEndianUint32(char *destination, const std::uint32_t value) {
    destination[0] = static_cast<char>((value >> 24) & 0xffU);
    destination[1] = static_cast<char>((value >> 16) & 0xffU);
    destination[2] = static_cast<char>((value >> 8) & 0xffU);
    destination[3] = static_cast<char>(value & 0xffU);
}

}  // namespace

std::string encodeEnvelopeFrame(const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    const std::string payload = envelope.SerializeAsString();

    std::string frame;
    frame.resize(kFrameHeaderSizeBytes + payload.size());
    writeBigEndianUint32(frame.data(), static_cast<std::uint32_t>(payload.size()));
    frame.replace(kFrameHeaderSizeBytes, payload.size(), payload);
    return frame;
}

std::uint32_t decodeFramePayloadSize(const char *headerBytes) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(headerBytes);
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

bool parseEnvelopePayload(
    const std::string &payload,
    qodex::threadui::ipc::common::RpcEnvelope *envelope,
    std::string *errorMessage
) {
    if (envelope == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Envelope destination was null.";
        }
        return false;
    }

    if (!envelope->ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to parse RpcEnvelope payload.";
        }
        return false;
    }

    return true;
}

FrameDecodeResult tryDecodeNextEnvelope(
    std::string *buffer,
    qodex::threadui::ipc::common::RpcEnvelope *envelope,
    std::string *errorMessage
) {
    if (buffer == nullptr || envelope == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Frame decode arguments were invalid.";
        }
        return FrameDecodeResult::InvalidFrame;
    }

    if (buffer->size() < kFrameHeaderSizeBytes) {
        return FrameDecodeResult::Incomplete;
    }

    const std::uint32_t payloadSize = decodeFramePayloadSize(buffer->data());
    if (payloadSize > kMaxEnvelopePayloadSizeBytes) {
        if (errorMessage != nullptr) {
            *errorMessage = "Envelope frame exceeded the maximum supported payload size.";
        }
        return FrameDecodeResult::InvalidFrame;
    }

    const std::size_t frameSize = kFrameHeaderSizeBytes + static_cast<std::size_t>(payloadSize);
    if (buffer->size() < frameSize) {
        return FrameDecodeResult::Incomplete;
    }

    const std::string payload = buffer->substr(kFrameHeaderSizeBytes, payloadSize);
    buffer->erase(0, frameSize);

    if (!parseEnvelopePayload(payload, envelope, errorMessage)) {
        return FrameDecodeResult::InvalidFrame;
    }

    return FrameDecodeResult::Success;
}

}  // namespace qodex::threadui::ipc
