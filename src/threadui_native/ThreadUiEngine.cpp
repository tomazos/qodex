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

#include "qodex_to_ui.qodex_rpc.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.qodex_rpc.h"

namespace qodex::threadui::native {

namespace {

bool initialized = false;
std::int64_t frameCount = 0;
qodex::threadui::native::LaunchConfig currentLaunchConfig;
FrameCountDisplayCallback frameCountDisplayCallback;
std::atomic<std::int64_t> highestTestPongValue{0};
struct IpcClientState;
std::unique_ptr<IpcClientState> ipcClientState;
namespace UiToQodexRpc = qodex::threadui::ipc::ui_to_qodex::rpc::UiToQodex;
namespace QodexToUiRpc = qodex::threadui::ipc::qodex_to_ui::rpc::QodexToUi;

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
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::TestPing>(requestId, request)
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
        qodex::threadui::ipc::makeResponseEnvelope<QodexToUiRpc::TestPong>(requestId, response)
    );
}

void handleEnvelope(IpcClientState *state, const qodex::threadui::ipc::common::RpcEnvelope &envelope) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    if (envelope.is_response()) {
        struct UiToQodexResponseHandler final {
            IpcClientState *state;

            bool onLoginResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::LoginResponse &response,
                std::string *errorMessage
            ) {
                if (requestId != state->pendingLoginRequestId) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a Login response for an unknown request id.";
                    }
                    return false;
                }

                state->pendingLoginRequestId = 0;
                if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Thread UI Login failed: " + response.message();
                    }
                    return false;
                }

                state->authenticated = true;
                std::cout << "Thread UI Login succeeded: " << response.message() << std::endl;
                sendTestPingRequest(state, 1);
                return true;
            }

            bool onTestPingResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::TestPingResponse &response,
                std::string *errorMessage
            ) {
                if (requestId != state->pendingTestPingRequestId) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a TestPing response for an unknown request id.";
                    }
                    return false;
                }

                const std::int64_t completedPingValue = state->pendingTestPingValue;
                state->pendingTestPingRequestId = 0;
                state->pendingTestPingValue = 0;
                if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Thread UI TestPing failed for value " +
                                        std::to_string(completedPingValue) + ": " + response.message();
                    }
                    return false;
                }

                return true;
            }
        } handler{state};

        std::string dispatchErrorMessage;
        if (!UiToQodexRpc::dispatchResponseEnvelope(envelope, handler, &dispatchErrorMessage)) {
            stopClientOnProtocolFailure(state, dispatchErrorMessage);
        }
        return;
    }

    struct QodexToUiRequestHandler final {
        IpcClientState *state;

        bool onTestPongRequest(
            const std::uint64_t requestId,
            const qodex::threadui::ipc::qodex_to_ui::TestPongRequest &request,
            std::string *
        ) {
            updateHighestTestPong(request.value());
            sendTestPongResponse(
                state,
                requestId,
                qodex::threadui::ipc::common::RESULT_STATUS_OK,
                "Test pong received."
            );
            sendTestPingRequest(state, request.value() + 1);
            return true;
        }
    } handler{state};

    std::string dispatchErrorMessage;
    if (!QodexToUiRpc::dispatchRequestEnvelope(envelope, handler, &dispatchErrorMessage)) {
        if (envelope.method() == QodexToUiRpc::TestPong::kMethodName) {
            sendTestPongResponse(
                state,
                envelope.request_id(),
                qodex::threadui::ipc::common::RESULT_STATUS_ERROR,
                dispatchErrorMessage
            );
        }
        stopClientOnProtocolFailure(state, dispatchErrorMessage);
    }
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
        qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::Login>(requestId, request)
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
