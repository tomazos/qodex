#include "threadui_native/ThreadUiEngine.h"

#include <asio.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "common.pb.h"
#include "qodex_to_ui.pb.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.pb.h"

namespace qodex::threadui::native {

namespace {

bool initialized = false;
std::int64_t frameCount = 0;
qodex::threadui::native::LaunchConfig currentLaunchConfig;
FrameCountDisplayCallback frameCountDisplayCallback;
std::atomic<std::int64_t> highestTestPongValue{0};
struct IpcClientState;
std::unique_ptr<IpcClientState> ipcClientState;

bool hasIpcTarget(const qodex::threadui::native::LaunchConfig &launchConfig) {
    return !launchConfig.host.empty() && launchConfig.port != 0;
}

struct IpcClientState final {
    explicit IpcClientState(const qodex::threadui::native::LaunchConfig &config)
        : launchConfig(config),
          workGuard(asio::make_work_guard(ioContext)),
          resolver(ioContext),
          socket(ioContext),
          reconnectTimer(ioContext) {
    }

    qodex::threadui::native::LaunchConfig launchConfig;
    asio::io_context ioContext;
    asio::executor_work_guard<asio::io_context::executor_type> workGuard;
    asio::ip::tcp::resolver resolver;
    asio::ip::tcp::socket socket;
    asio::steady_timer reconnectTimer;
    std::thread thread;
    std::array<char, 4096> readChunk{};
    std::string inputBuffer;
    std::deque<std::string> writeQueue;
    std::uint64_t nextRequestId = 1;
    std::uint64_t pendingLoginRequestId = 0;
    std::uint64_t pendingTestPingRequestId = 0;
    std::int64_t pendingTestPingValue = 0;
    bool stopRequested = false;
    bool connected = false;
    bool authenticated = false;
    bool writeInProgress = false;
};

std::string endpointDescription(const qodex::threadui::native::LaunchConfig &launchConfig) {
    return launchConfig.host + ":" + std::to_string(launchConfig.port);
}

void scheduleConnect(IpcClientState *state);
void scheduleReconnect(IpcClientState *state, const asio::error_code &errorCode);

void stopIpcClient();

qodex::threadui::ipc::common::RpcEnvelope makeRequestEnvelope(
    const std::uint64_t requestId,
    const char *methodName,
    const std::string &payload
) {
    qodex::threadui::ipc::common::RpcEnvelope envelope;
    envelope.set_request_id(requestId);
    envelope.set_is_response(false);
    envelope.set_method(methodName);
    envelope.set_payload(payload);
    return envelope;
}

qodex::threadui::ipc::common::RpcEnvelope makeResponseEnvelope(
    const std::uint64_t requestId,
    const char *methodName,
    const std::string &payload
) {
    qodex::threadui::ipc::common::RpcEnvelope envelope;
    envelope.set_request_id(requestId);
    envelope.set_is_response(true);
    envelope.set_method(methodName);
    envelope.set_payload(payload);
    return envelope;
}

void beginWriteQueuedFrames(IpcClientState *state);

void queueFrameForWrite(IpcClientState *state, std::string frame) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    state->writeQueue.push_back(std::move(frame));
    if (!state->writeInProgress) {
        beginWriteQueuedFrames(state);
    }
}

void beginWriteQueuedFrames(IpcClientState *state) {
    if (state == nullptr || state->stopRequested || state->writeQueue.empty()) {
        return;
    }

    state->writeInProgress = true;
    asio::async_write(
        state->socket,
        asio::buffer(state->writeQueue.front()),
        [state](const asio::error_code &writeError, const std::size_t) {
            if (state->stopRequested) {
                return;
            }

            if (writeError) {
                scheduleReconnect(state, writeError);
                return;
            }

            state->writeQueue.pop_front();
            if (state->writeQueue.empty()) {
                state->writeInProgress = false;
                return;
            }

            beginWriteQueuedFrames(state);
        }
    );
}

void queueEnvelopeForWrite(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    queueFrameForWrite(state, qodex::threadui::ipc::encodeEnvelopeFrame(envelope));
}

void updateHighestTestPong(const std::int64_t value) {
    auto currentHighest = highestTestPongValue.load(std::memory_order_relaxed);
    while (value > currentHighest &&
           !highestTestPongValue.compare_exchange_weak(
               currentHighest,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed
           )) {
    }
}

void scheduleReconnect(IpcClientState *state, const asio::error_code &errorCode) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (errorCode != asio::error::operation_aborted) {
        std::cerr << "Thread UI IPC reconnect scheduled for "
                  << endpointDescription(state->launchConfig)
                  << ": "
                  << errorCode.message()
                  << std::endl;
    }

    state->connected = false;
    state->authenticated = false;
    state->pendingLoginRequestId = 0;
    state->pendingTestPingRequestId = 0;
    state->pendingTestPingValue = 0;
    state->inputBuffer.clear();
    state->writeQueue.clear();
    state->writeInProgress = false;
    state->reconnectTimer.expires_after(std::chrono::milliseconds(100));
    state->reconnectTimer.async_wait([state](const asio::error_code &timerError) {
        if (timerError == asio::error::operation_aborted || state->stopRequested) {
            return;
        }

        scheduleConnect(state);
    });
}

void stopClientOnProtocolFailure(IpcClientState *state, const std::string &message) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    std::cerr << message << std::endl;
    state->stopRequested = true;
    asio::error_code ignoredError;
    state->reconnectTimer.cancel();
    state->resolver.cancel();
    state->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
    state->socket.close(ignoredError);
    state->workGuard.reset();
}

void beginReadFromSocket(IpcClientState *state);

void sendTestPingRequest(IpcClientState *state, const std::int64_t value) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (state->pendingTestPingRequestId != 0) {
        stopClientOnProtocolFailure(state, "Attempted to send TestPing while another TestPing is still pending.");
        return;
    }

    qodex::threadui::ipc::ui_to_qodex::TestPingRequest request;
    request.set_value(value);

    const std::uint64_t requestId = state->nextRequestId++;
    state->pendingTestPingRequestId = requestId;
    state->pendingTestPingValue = value;
    queueEnvelopeForWrite(
        state,
        makeRequestEnvelope(requestId, qodex::threadui::ipc::kTestPingMethodName, request.SerializeAsString())
    );
}

void sendTestPongResponse(
    IpcClientState *state,
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const std::string &message
) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::TestPongResponse response;
    response.set_status(status);
    response.set_message(message);
    queueEnvelopeForWrite(
        state,
        makeResponseEnvelope(requestId, qodex::threadui::ipc::kTestPongMethodName, response.SerializeAsString())
    );
}

void handleResponseEnvelope(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    if (envelope.method() == qodex::threadui::ipc::kLoginMethodName) {
        if (envelope.request_id() != state->pendingLoginRequestId) {
            stopClientOnProtocolFailure(state, "Received a Login response for an unknown request id.");
            return;
        }

        qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
        if (!response.ParseFromString(envelope.payload())) {
            stopClientOnProtocolFailure(state, "Failed to parse Thread UI Login response.");
            return;
        }

        state->pendingLoginRequestId = 0;
        if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
            stopClientOnProtocolFailure(state, "Thread UI Login failed: " + response.message());
            return;
        }

        state->authenticated = true;
        std::cout << "Thread UI Login succeeded: " << response.message() << std::endl;
        sendTestPingRequest(state, 1);
        return;
    }

    if (envelope.method() == qodex::threadui::ipc::kTestPingMethodName) {
        if (envelope.request_id() != state->pendingTestPingRequestId) {
            stopClientOnProtocolFailure(state, "Received a TestPing response for an unknown request id.");
            return;
        }

        qodex::threadui::ipc::ui_to_qodex::TestPingResponse response;
        if (!response.ParseFromString(envelope.payload())) {
            stopClientOnProtocolFailure(state, "Failed to parse Thread UI TestPing response.");
            return;
        }

        const std::int64_t completedPingValue = state->pendingTestPingValue;
        state->pendingTestPingRequestId = 0;
        state->pendingTestPingValue = 0;
        if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
            stopClientOnProtocolFailure(
                state,
                "Thread UI TestPing failed for value " + std::to_string(completedPingValue) + ": " + response.message()
            );
            return;
        }
        return;
    }

    stopClientOnProtocolFailure(state, "Received an unexpected Thread UI IPC response envelope.");
}

void handleRequestEnvelope(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    if (envelope.method() != qodex::threadui::ipc::kTestPongMethodName) {
        stopClientOnProtocolFailure(state, "Received an unexpected Thread UI IPC request envelope.");
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::TestPongRequest request;
    if (!request.ParseFromString(envelope.payload())) {
        sendTestPongResponse(
            state,
            envelope.request_id(),
            qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
            "Failed to parse QodexToUi.TestPong request payload."
        );
        stopClientOnProtocolFailure(state, "Failed to parse Thread UI TestPong request.");
        return;
    }

    updateHighestTestPong(request.value());
    sendTestPongResponse(
        state,
        envelope.request_id(),
        qodex::threadui::ipc::common::RESULT_STATUS_OK,
        "Test pong received."
    );
    sendTestPingRequest(state, request.value() + 1);
}

void handleEnvelope(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (envelope.is_response()) {
        handleResponseEnvelope(state, envelope);
        return;
    }

    handleRequestEnvelope(state, envelope);
}

void beginReadFromSocket(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    state->socket.async_read_some(
        asio::buffer(state->readChunk),
        [state](const asio::error_code &readError, const std::size_t bytesRead) {
            if (state->stopRequested) {
                return;
            }

            if (readError) {
                scheduleReconnect(state, readError);
                return;
            }

            state->inputBuffer.append(state->readChunk.data(), bytesRead);

            while (true) {
                qodex::threadui::ipc::common::RpcEnvelope envelope;
                std::string parseErrorMessage;
                const qodex::threadui::ipc::FrameDecodeResult decodeResult =
                    qodex::threadui::ipc::tryDecodeNextEnvelope(&state->inputBuffer, &envelope, &parseErrorMessage);
                if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::Incomplete) {
                    break;
                }

                if (decodeResult == qodex::threadui::ipc::FrameDecodeResult::InvalidFrame) {
                    stopClientOnProtocolFailure(state, parseErrorMessage);
                    return;
                }

                handleEnvelope(state, envelope);
                if (state->stopRequested) {
                    return;
                }
            }

            beginReadFromSocket(state);
        }
    );
}

void sendLoginRequest(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::ui_to_qodex::LoginRequest request;
    request.set_token(state->launchConfig.token);

    const std::uint64_t requestId = state->nextRequestId++;
    state->pendingLoginRequestId = requestId;
    queueEnvelopeForWrite(
        state,
        makeRequestEnvelope(requestId, qodex::threadui::ipc::kLoginMethodName, request.SerializeAsString())
    );
    std::cout << "Sent Thread UI Login request." << std::endl;
}

void scheduleConnect(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    asio::error_code closeError;
    state->socket.close(closeError);
    state->resolver.async_resolve(
        state->launchConfig.host,
        std::to_string(state->launchConfig.port),
        [state](const asio::error_code &resolveError, asio::ip::tcp::resolver::results_type endpoints) {
            if (state->stopRequested) {
                return;
            }

            if (resolveError) {
                scheduleReconnect(state, resolveError);
                return;
            }

            asio::async_connect(
                state->socket,
                endpoints,
                [state](const asio::error_code &connectError, const asio::ip::tcp::endpoint &) {
                    if (state->stopRequested) {
                        return;
                    }

                    if (connectError) {
                        scheduleReconnect(state, connectError);
                        return;
                    }

                    asio::error_code noDelayError;
                    state->socket.set_option(asio::ip::tcp::no_delay(true), noDelayError);
                    if (noDelayError) {
                        scheduleReconnect(state, noDelayError);
                        return;
                    }

                    state->connected = true;
                    state->authenticated = false;
                    state->pendingLoginRequestId = 0;
                    state->pendingTestPingRequestId = 0;
                    state->pendingTestPingValue = 0;
                    state->inputBuffer.clear();
                    state->writeQueue.clear();
                    state->writeInProgress = false;
                    std::cout << "Connected Thread UI IPC socket to "
                              << endpointDescription(state->launchConfig)
                              << '.'
                              << std::endl;
                    beginReadFromSocket(state);
                    sendLoginRequest(state);
                }
            );
        }
    );
}

void startIpcClient(const qodex::threadui::native::LaunchConfig &launchConfig) {
    auto state = std::make_unique<IpcClientState>(launchConfig);
    IpcClientState *statePtr = state.get();

    asio::post(state->ioContext, [statePtr] {
        scheduleConnect(statePtr);
    });

    state->thread = std::thread([statePtr] {
        statePtr->ioContext.run();
    });

    ipcClientState = std::move(state);
}

void stopIpcClient() {
    if (!ipcClientState) {
        return;
    }

    ipcClientState->stopRequested = true;
    IpcClientState *state = ipcClientState.get();
    asio::post(state->ioContext, [state] {
        asio::error_code ignoredError;
        state->resolver.cancel();
        state->reconnectTimer.cancel();
        state->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
        state->socket.close(ignoredError);
        state->workGuard.reset();
    });

    if (ipcClientState->thread.joinable()) {
        ipcClientState->thread.join();
    }

    ipcClientState.reset();
}

bool isPowerOfTwo(const std::int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace

void initialize(const LaunchConfig &launchConfig) {
    if (initialized) {
        return;
    }

    initialized = true;
    frameCount = 0;
    currentLaunchConfig = launchConfig;
    highestTestPongValue.store(0, std::memory_order_relaxed);

    std::cout << "Qodex thread UI engine initialized.";
    if (hasIpcTarget(currentLaunchConfig)) {
        std::cout << " Qodex IPC target: " << currentLaunchConfig.host << ':' << currentLaunchConfig.port << '.';
        startIpcClient(currentLaunchConfig);
    } else {
        std::cout << " Qodex IPC target is not configured.";
    }
    std::cout << std::endl;
}

void setFrameCountDisplayCallback(FrameCountDisplayCallback callback) {
    frameCountDisplayCallback = std::move(callback);
}

std::int64_t highestTestPong() noexcept {
    return highestTestPongValue.load(std::memory_order_relaxed);
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
    stopIpcClient();
    currentLaunchConfig = {};
    frameCountDisplayCallback = nullptr;
    highestTestPongValue.store(0, std::memory_order_relaxed);
    std::cout << "Qodex thread UI engine shutdown." << std::endl;
}

}  // namespace qodex::threadui::native
