#include "threadui/ThreadUiIpcFraming.h"

#include <algorithm>
#include <cstddef>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

namespace qodex::threadui::ipc {

namespace {

FrameDecodeResult tryDecodeLengthPrefix(
    const std::string &buffer,
    std::uint32_t *payloadSize,
    std::size_t *prefixSize,
    std::string *errorMessage
) {
    if (payloadSize == nullptr || prefixSize == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Frame length decode arguments were invalid.";
        }
        return FrameDecodeResult::InvalidFrame;
    }

    const std::size_t bytesToInspect = std::min(buffer.size(), kMaxEnvelopeLengthPrefixBytes);
    for (std::size_t index = 0; index < bytesToInspect; ++index) {
        const unsigned char byte = static_cast<unsigned char>(buffer[index]);
        if ((byte & 0x80U) != 0U) {
            continue;
        }

        google::protobuf::io::ArrayInputStream inputStream(buffer.data(), static_cast<int>(index + 1));
        google::protobuf::io::CodedInputStream codedInput(&inputStream);
        std::uint32_t decodedPayloadSize = 0;
        if (!codedInput.ReadVarint32(&decodedPayloadSize)) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to decode envelope frame length prefix.";
            }
            return FrameDecodeResult::InvalidFrame;
        }

        *payloadSize = decodedPayloadSize;
        *prefixSize = index + 1;
        return FrameDecodeResult::Success;
    }

    if (buffer.size() < kMaxEnvelopeLengthPrefixBytes) {
        return FrameDecodeResult::Incomplete;
    }

    if (errorMessage != nullptr) {
        *errorMessage = "Envelope frame length prefix exceeded the maximum varint size.";
    }
    return FrameDecodeResult::InvalidFrame;
}

}  // namespace

std::string encodeEnvelopeFrame(const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    const std::string payload = envelope.SerializeAsString();
    const std::size_t prefixSize =
        google::protobuf::io::CodedOutputStream::VarintSize32(static_cast<std::uint32_t>(payload.size()));

    std::string frame;
    frame.resize(prefixSize + payload.size());

    auto *target = reinterpret_cast<std::uint8_t *>(frame.data());
    target = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(
        static_cast<std::uint32_t>(payload.size()),
        target
    );
    google::protobuf::io::CodedOutputStream::WriteRawToArray(
        payload.data(),
        static_cast<int>(payload.size()),
        target
    );
    return frame;
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

    std::uint32_t payloadSize = 0;
    std::size_t prefixSize = 0;
    const FrameDecodeResult prefixDecodeResult =
        tryDecodeLengthPrefix(*buffer, &payloadSize, &prefixSize, errorMessage);
    if (prefixDecodeResult != FrameDecodeResult::Success) {
        return prefixDecodeResult;
    }

    const std::size_t frameSize = prefixSize + static_cast<std::size_t>(payloadSize);
    if (buffer->size() < frameSize) {
        return FrameDecodeResult::Incomplete;
    }

    const std::string payload = buffer->substr(prefixSize, payloadSize);
    buffer->erase(0, frameSize);

    if (!parseEnvelopePayload(payload, envelope, errorMessage)) {
        return FrameDecodeResult::InvalidFrame;
    }

    return FrameDecodeResult::Success;
}

}  // namespace qodex::threadui::ipc
