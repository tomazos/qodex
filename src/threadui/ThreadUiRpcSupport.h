#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "common.pb.h"

namespace qodex::threadui::ipc {

template <typename Method>
common::RpcEnvelope makeRequestEnvelope(
    const std::uint64_t requestId,
    const typename Method::Request &request
) {
    common::RpcEnvelope envelope;
    envelope.set_request_id(requestId);
    envelope.set_is_response(false);
    envelope.set_method(std::string(Method::kMethodName));
    envelope.set_payload(request.SerializeAsString());
    return envelope;
}

template <typename Method>
common::RpcEnvelope makeResponseEnvelope(
    const std::uint64_t requestId,
    const typename Method::Response &response
) {
    common::RpcEnvelope envelope;
    envelope.set_request_id(requestId);
    envelope.set_is_response(true);
    envelope.set_method(std::string(Method::kMethodName));
    envelope.set_payload(response.SerializeAsString());
    return envelope;
}

template <typename Method>
bool parseRequestEnvelope(
    const common::RpcEnvelope &envelope,
    typename Method::Request *request,
    std::string *errorMessage = nullptr
) {
    if (request == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Request destination was null.";
        }
        return false;
    }

    if (envelope.is_response()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Expected a request envelope for method " + std::string(Method::kMethodName) + '.';
        }
        return false;
    }

    if (envelope.method() != Method::kMethodName) {
        if (errorMessage != nullptr) {
            *errorMessage = "Expected request method " + std::string(Method::kMethodName) + ", got " + envelope.method() +
                            '.';
        }
        return false;
    }

    if (!request->ParseFromString(envelope.payload())) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to parse request payload for method " + std::string(Method::kMethodName) + '.';
        }
        return false;
    }

    return true;
}

template <typename Method>
bool parseResponseEnvelope(
    const common::RpcEnvelope &envelope,
    typename Method::Response *response,
    std::string *errorMessage = nullptr
) {
    if (response == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Response destination was null.";
        }
        return false;
    }

    if (!envelope.is_response()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Expected a response envelope for method " + std::string(Method::kMethodName) + '.';
        }
        return false;
    }

    if (envelope.method() != Method::kMethodName) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Expected response method " + std::string(Method::kMethodName) + ", got " + envelope.method() + '.';
        }
        return false;
    }

    if (!response->ParseFromString(envelope.payload())) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to parse response payload for method " + std::string(Method::kMethodName) + '.';
        }
        return false;
    }

    return true;
}

}  // namespace qodex::threadui::ipc
