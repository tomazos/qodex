#include "threadui_native/ThreadUiEngine.h"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
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
std::mutex fatalErrorMutex;
std::string pendingFatalError;
std::mutex pendingErrorMutex;
std::string pendingError;
std::mutex pendingItemsMutex;
std::vector<qodex::threadui::native::DisplayItem> pendingItems;
struct IpcClientState;
std::unique_ptr<IpcClientState> ipcClientState;
namespace QodexToUiRpc = qodex::threadui::ipc::qodex_to_ui::rpc::QodexToUi;
namespace UiToQodexRpc = qodex::threadui::ipc::ui_to_qodex::rpc::UiToQodex;

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
    std::deque<std::string> queuedUserInputTexts;
    std::unordered_set<std::uint64_t> pendingSendUserInputRequestIds;
    std::uint64_t nextRequestId = 1;
    std::uint64_t pendingLoginRequestId = 0;
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

void recordFatalError(const std::string &message) {
    if (message.empty()) {
        return;
    }

    std::lock_guard lock(fatalErrorMutex);
    if (pendingFatalError.empty()) {
        pendingFatalError = message;
    }
}

void recordPendingError(const std::string &message) {
    if (message.empty()) {
        return;
    }

    std::lock_guard lock(pendingErrorMutex);
    if (pendingError.empty()) {
        pendingError = message;
    }
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

void flushQueuedUserInput(IpcClientState *state) {
    if (state == nullptr || state->stopRequested || !state->authenticated) {
        return;
    }

    while (!state->queuedUserInputTexts.empty()) {
        qodex::threadui::ipc::ui_to_qodex::SendUserInputRequest request;
        request.set_text(state->queuedUserInputTexts.front());

        const std::uint64_t requestId = state->nextRequestId++;
        state->pendingSendUserInputRequestIds.insert(requestId);
        queueEnvelopeForWrite(
            state,
            qodex::threadui::ipc::makeRequestEnvelope<UiToQodexRpc::SendUserInput>(requestId, request)
        );
        state->queuedUserInputTexts.pop_front();
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
    recordFatalError(message);
    state->stopRequested = true;
    asio::error_code ignoredError;
    state->reconnectTimer.cancel();
    state->resolver.cancel();
    state->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignoredError);
    state->socket.close(ignoredError);
    state->workGuard.reset();
}

void beginReadFromSocket(IpcClientState *state);

void queueDisplayItems(const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request) {
    std::lock_guard lock(pendingItemsMutex);
    pendingItems.reserve(pendingItems.size() + static_cast<std::size_t>(request.items_size()));

    for (const auto &item : request.items()) {
        switch (item.kind_case()) {
        case qodex::threadui::ipc::common::Item::kUserMessage:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .kind = qodex::threadui::native::DisplayItemKind::UserMessage,
                .text = item.user_message().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::kAgentMessage:
            pendingItems.push_back(qodex::threadui::native::DisplayItem{
                .kind = qodex::threadui::native::DisplayItemKind::AgentMessage,
                .text = item.agent_message().text(),
            });
            break;
        case qodex::threadui::ipc::common::Item::KIND_NOT_SET:
            break;
        }
    }
}

void sendAddItemsResponse(
    IpcClientState *state,
    const std::uint64_t requestId,
    const qodex::threadui::ipc::common::ResultStatus status,
    const std::string &message
) {
    if (state == nullptr || state->stopRequested) {
        return;
    }

    qodex::threadui::ipc::qodex_to_ui::AddItemsResponse response;
    response.set_status(status);
    response.set_message(message);
    queueEnvelopeForWrite(
        state,
        qodex::threadui::ipc::makeResponseEnvelope<QodexToUiRpc::AddItems>(requestId, response)
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
                flushQueuedUserInput(state);
                return true;
            }

            bool onSendUserInputResponse(
                const std::uint64_t requestId,
                const qodex::threadui::ipc::ui_to_qodex::SendUserInputResponse &response,
                std::string *errorMessage
            ) {
                if (!state->pendingSendUserInputRequestIds.contains(requestId)) {
                    if (errorMessage != nullptr) {
                        *errorMessage = "Received a SendUserInput response for an unknown request id.";
                    }
                    return false;
                }

                state->pendingSendUserInputRequestIds.erase(requestId);
                if (response.status() != qodex::threadui::ipc::common::RESULT_STATUS_OK) {
                    recordPendingError("SendUserInput failed: " + response.message());
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

        bool onAddItemsRequest(
            const std::uint64_t requestId,
            const qodex::threadui::ipc::qodex_to_ui::AddItemsRequest &request,
            std::string *
        ) {
            queueDisplayItems(request);
            sendAddItemsResponse(
                state,
                requestId,
                qodex::threadui::ipc::common::RESULT_STATUS_OK,
                "Items added."
            );
            return true;
        }
    } handler{state};

    std::string dispatchErrorMessage;
    if (!QodexToUiRpc::dispatchRequestEnvelope(envelope, handler, &dispatchErrorMessage)) {
        if (envelope.method() == QodexToUiRpc::AddItems::kMethodName) {
            sendAddItemsResponse(
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
                    state->pendingSendUserInputRequestIds.clear();
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
    {
        std::lock_guard lock(fatalErrorMutex);
        pendingFatalError.clear();
    }
    {
        std::lock_guard lock(pendingErrorMutex);
        pendingError.clear();
    }
    {
        std::lock_guard lock(pendingItemsMutex);
        pendingItems.clear();
    }

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

std::vector<DisplayItem> takePendingItems() {
    std::lock_guard lock(pendingItemsMutex);
    std::vector<DisplayItem> items = std::move(pendingItems);
    pendingItems.clear();
    return items;
}

std::string takeFatalError() {
    std::lock_guard lock(fatalErrorMutex);
    std::string message = std::move(pendingFatalError);
    pendingFatalError.clear();
    return message;
}

std::string takePendingError() {
    std::lock_guard lock(pendingErrorMutex);
    std::string message = std::move(pendingError);
    pendingError.clear();
    return message;
}

void sendUserInput(const std::string &text) {
    if (!initialized || text.empty() || !ipcClientState) {
        if (initialized && text.empty()) {
            recordPendingError("Input must not be empty.");
        } else if (!initialized || !ipcClientState) {
            recordPendingError("Thread UI is not connected.");
        }
        return;
    }

    IpcClientState *state = ipcClientState.get();
    asio::post(state->ioContext, [state, text] {
        if (state->stopRequested) {
            return;
        }

        state->queuedUserInputTexts.push_back(text);
        flushQueuedUserInput(state);
    });
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
    {
        std::lock_guard lock(fatalErrorMutex);
        pendingFatalError.clear();
    }
    {
        std::lock_guard lock(pendingErrorMutex);
        pendingError.clear();
    }
    {
        std::lock_guard lock(pendingItemsMutex);
        pendingItems.clear();
    }
    std::cout << "Qodex thread UI engine shutdown." << std::endl;
}

}  // namespace qodex::threadui::native
