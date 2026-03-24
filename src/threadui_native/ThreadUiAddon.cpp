#include <node_api.h>

#include <cstdint>

#include "threadui_native/NativeAdd.h"
#include "threadui_native/ThreadUiEngine.h"

namespace {

napi_env callbackEnv = nullptr;
napi_ref frameCountDisplayCallbackRef = nullptr;

void throwTypeError(napi_env env, const char *message) {
    napi_throw_type_error(env, nullptr, message);
}

napi_value getUndefined(napi_env env) {
    napi_value undefinedValue = nullptr;

    if (napi_get_undefined(env, &undefinedValue) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create undefined return value");
        return nullptr;
    }

    return undefinedValue;
}

void clearFrameCountDisplayCallback() {
    if (callbackEnv != nullptr && frameCountDisplayCallbackRef != nullptr) {
        napi_delete_reference(callbackEnv, frameCountDisplayCallbackRef);
    }

    callbackEnv = nullptr;
    frameCountDisplayCallbackRef = nullptr;
    qodex::threadui::native::setFrameCountDisplayCallback(nullptr);
}

void invokeFrameCountDisplayCallback(const std::int64_t frameCount) {
    if (callbackEnv == nullptr || frameCountDisplayCallbackRef == nullptr) {
        return;
    }

    napi_handle_scope scope = nullptr;
    if (napi_open_handle_scope(callbackEnv, &scope) != napi_ok) {
        return;
    }

    napi_value callback = nullptr;
    napi_value global = nullptr;
    napi_value argument = nullptr;

    if (napi_get_reference_value(callbackEnv, frameCountDisplayCallbackRef, &callback) == napi_ok &&
        napi_get_global(callbackEnv, &global) == napi_ok &&
        napi_create_bigint_int64(callbackEnv, frameCount, &argument) == napi_ok) {
        napi_value ignoredResult = nullptr;
        napi_call_function(callbackEnv, global, callback, 1, &argument, &ignoredResult);
    }

    napi_close_handle_scope(callbackEnv, scope);
}

napi_value addWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];

    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 2) {
        throwTypeError(env, "add expects exactly 2 arguments");
        return nullptr;
    }

    int32_t a = 0;
    int32_t b = 0;

    if (napi_get_value_int32(env, args[0], &a) != napi_ok) {
        throwTypeError(env, "First argument must be a 32-bit integer");
        return nullptr;
    }

    if (napi_get_value_int32(env, args[1], &b) != napi_ok) {
        throwTypeError(env, "Second argument must be a 32-bit integer");
        return nullptr;
    }

    napi_value result = nullptr;
    if (napi_create_int32(env, qodex::threadui::native::add(a, b), &result) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create return value");
        return nullptr;
    }

    return result;
}

napi_value initializeWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "initialize expects no arguments");
        return nullptr;
    }

    qodex::threadui::native::initialize();
    return getUndefined(env);
}

napi_value setFrameCountDisplayCallbackWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];

    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 1) {
        throwTypeError(env, "setFrameCountDisplayCallback expects exactly 1 argument");
        return nullptr;
    }

    napi_valuetype valueType;
    if (napi_typeof(env, args[0], &valueType) != napi_ok || valueType != napi_function) {
        throwTypeError(env, "setFrameCountDisplayCallback expects a function");
        return nullptr;
    }

    clearFrameCountDisplayCallback();

    callbackEnv = env;
    if (napi_create_reference(env, args[0], 1, &frameCountDisplayCallbackRef) != napi_ok) {
        callbackEnv = nullptr;
        frameCountDisplayCallbackRef = nullptr;
        napi_throw_error(env, nullptr, "Failed to store frame count callback");
        return nullptr;
    }

    qodex::threadui::native::setFrameCountDisplayCallback(invokeFrameCountDisplayCallback);
    return getUndefined(env);
}

napi_value tickWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "tick expects no arguments");
        return nullptr;
    }

    qodex::threadui::native::tick();
    return getUndefined(env);
}

napi_value shutdownWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "shutdown expects no arguments");
        return nullptr;
    }

    qodex::threadui::native::shutdown();
    clearFrameCountDisplayCallback();
    return getUndefined(env);
}

napi_status exportFunction(
    napi_env env,
    napi_value exports,
    const char *name,
    napi_callback callback
) {
    napi_value function = nullptr;

    if (napi_create_function(env, name, NAPI_AUTO_LENGTH, callback, nullptr, &function) != napi_ok) {
        return napi_generic_failure;
    }

    return napi_set_named_property(env, exports, name, function);
}

napi_value init(napi_env env, napi_value exports) {
    if (exportFunction(env, exports, "add", addWrapped) != napi_ok ||
        exportFunction(env, exports, "initialize", initializeWrapped) != napi_ok ||
        exportFunction(env, exports, "setFrameCountDisplayCallback", setFrameCountDisplayCallbackWrapped) != napi_ok ||
        exportFunction(env, exports, "tick", tickWrapped) != napi_ok ||
        exportFunction(env, exports, "shutdown", shutdownWrapped) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to export native functions");
        return nullptr;
    }

    return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
