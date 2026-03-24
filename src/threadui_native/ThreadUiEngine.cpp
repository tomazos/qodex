#include "threadui_native/ThreadUiEngine.h"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "common.pb.h"
#include "threadui/ThreadUiIpcFraming.h"
#include "ui_to_qodex.pb.h"

namespace qodex::threadui::native {

namespace {

bool initialized = false;
std::int64_t frameCount = 0;
qodex::threadui::native::LaunchConfig currentLaunchConfig;
FrameCountDisplayCallback frameCountDisplayCallback;
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
    std::array<char, qodex::threadui::ipc::kFrameHeaderSizeBytes> readHeader{};
    std::string readPayload;
    std::uint64_t nextRequestId = 1;
    bool stopRequested = false;
    bool connected = false;
    bool authenticated = false;
};

std::string endpointDescription(const qodex::threadui::native::LaunchConfig &launchConfig) {
    return launchConfig.host + ":" + std::to_string(launchConfig.port);
}

void scheduleConnect(IpcClientState *state);

void stopIpcClient();

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

void beginReadEnvelopeHeader(IpcClientState *state);

void beginReadEnvelopeBody(IpcClientState *state, const std::uint32_t payloadSize) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    state->readPayload.assign(payloadSize, '\0');
    asio::async_read(
        state->socket,
        asio::buffer(state->readPayload.data(), state->readPayload.size()),
        [state](const asio::error_code &readError, const std::size_t) {
            if (state->stopRequested) {
                return;
            }

            if (readError) {
                scheduleReconnect(state, readError);
                return;
            }

            qodex::threadui::ipc::common::RpcEnvelope envelope;
            std::string parseErrorMessage;
            if (!qodex::threadui::ipc::parseEnvelopePayload(state->readPayload, &envelope, &parseErrorMessage)) {
                stopClientOnProtocolFailure(state, parseErrorMessage);
                return;
            }

            if (!envelope.is_response() ||
                envelope.service() != qodex::threadui::ipc::common::RPC_SERVICE_UI_TO_QODEX ||
                envelope.method() != qodex::threadui::ipc::kLoginMethodName) {
                stopClientOnProtocolFailure(state, "Received an unexpected Thread UI IPC envelope.");
                return;
            }

            qodex::threadui::ipc::ui_to_qodex::LoginResponse response;
            if (!response.ParseFromString(envelope.payload())) {
                stopClientOnProtocolFailure(state, "Failed to parse Thread UI Login response.");
                return;
            }

            if (response.status() == qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                state->authenticated = true;
                std::cout << "Thread UI Login succeeded: " << response.message() << std::endl;
            } else {
                stopClientOnProtocolFailure(
                    state,
                    "Thread UI Login failed: " + response.message()
                );
                return;
            }

            beginReadEnvelopeHeader(state);
        }
    );
}

void beginReadEnvelopeHeader(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    asio::async_read(
        state->socket,
        asio::buffer(state->readHeader),
        [state](const asio::error_code &readError, const std::size_t) {
            if (state->stopRequested) {
                return;
            }

            if (readError) {
                scheduleReconnect(state, readError);
                return;
            }

            const std::uint32_t payloadSize = qodex::threadui::ipc::decodeFramePayloadSize(state->readHeader.data());
            if (payloadSize > qodex::threadui::ipc::kMaxEnvelopePayloadSizeBytes) {
                stopClientOnProtocolFailure(state, "Received an oversized Thread UI IPC envelope.");
                return;
            }

            beginReadEnvelopeBody(state, payloadSize);
        }
    );
}

void sendLoginRequest(IpcClientState *state) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::ui_to_qodex::LoginRequest request;
    request.set_token(state->launchConfig.token);

    qodex::threadui::ipc::common::RpcEnvelope envelope;
    envelope.set_request_id(state->nextRequestId++);
    envelope.set_is_response(false);
    envelope.set_service(qodex::threadui::ipc::common::RPC_SERVICE_UI_TO_QODEX);
    envelope.set_method(qodex::threadui::ipc::kLoginMethodName);
    envelope.set_payload(request.SerializeAsString());

    auto frame = std::make_shared<std::string>(qodex::threadui::ipc::encodeEnvelopeFrame(envelope));
    asio::async_write(
        state->socket,
        asio::buffer(*frame),
        [state, frame](const asio::error_code &writeError, const std::size_t) {
            if (state->stopRequested) {
                return;
            }

            if (writeError) {
                scheduleReconnect(state, writeError);
                return;
            }

            std::cout << "Sent Thread UI Login request." << std::endl;
        }
    );
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

                    state->connected = true;
                    state->authenticated = false;
                    std::cout << "Connected Thread UI IPC socket to "
                              << endpointDescription(state->launchConfig)
                              << '.'
                              << std::endl;
                    beginReadEnvelopeHeader(state);
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
    std::cout << "Qodex thread UI engine shutdown." << std::endl;
}

}  // namespace qodex::threadui::native
