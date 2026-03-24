#include <node_api.h>

#include <cstdint>
#include <string>
#include <vector>

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

bool readHasNamedProperty(napi_env env, napi_value object, const char *name, bool *hasProperty) {
    if (napi_has_named_property(env, object, name, hasProperty) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to inspect launch config");
        return false;
    }

    return true;
}

bool readOptionalStringProperty(
    napi_env env,
    napi_value object,
    const char *name,
    std::string *outValue
) {
    bool hasProperty = false;
    if (!readHasNamedProperty(env, object, name, &hasProperty)) {
        return false;
    }

    if (!hasProperty) {
        return true;
    }

    napi_value property = nullptr;
    if (napi_get_named_property(env, object, name, &property) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read launch config property");
        return false;
    }

    napi_valuetype valueType = napi_undefined;
    if (napi_typeof(env, property, &valueType) != napi_ok || valueType != napi_string) {
        throwTypeError(env, "Launch config string properties must be strings");
        return false;
    }

    size_t valueLength = 0;
    if (napi_get_value_string_utf8(env, property, nullptr, 0, &valueLength) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to measure launch config string");
        return false;
    }

    outValue->assign(valueLength + 1, '\0');
    size_t copiedLength = 0;
    if (napi_get_value_string_utf8(env, property, outValue->data(), outValue->size(), &copiedLength) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read launch config string");
        return false;
    }

    outValue->resize(copiedLength);
    return true;
}

bool readOptionalPortProperty(
    napi_env env,
    napi_value object,
    const char *name,
    std::uint16_t *outValue
) {
    bool hasProperty = false;
    if (!readHasNamedProperty(env, object, name, &hasProperty)) {
        return false;
    }

    if (!hasProperty) {
        return true;
    }

    napi_value property = nullptr;
    if (napi_get_named_property(env, object, name, &property) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read launch config property");
        return false;
    }

    napi_valuetype valueType = napi_undefined;
    if (napi_typeof(env, property, &valueType) != napi_ok || valueType != napi_number) {
        throwTypeError(env, "Launch config port must be a number");
        return false;
    }

    std::uint32_t portValue = 0;
    if (napi_get_value_uint32(env, property, &portValue) != napi_ok || portValue > UINT16_MAX) {
        throwTypeError(env, "Launch config port must be an integer between 0 and 65535");
        return false;
    }

    *outValue = static_cast<std::uint16_t>(portValue);
    return true;
}

bool readLaunchConfig(
    napi_env env,
    const napi_value value,
    qodex::threadui::native::LaunchConfig *outLaunchConfig
) {
    napi_valuetype valueType = napi_undefined;
    if (napi_typeof(env, value, &valueType) != napi_ok || valueType != napi_object) {
        throwTypeError(env, "initialize expects a launch config object");
        return false;
    }

    return readOptionalStringProperty(env, value, "host", &outLaunchConfig->host) &&
           readOptionalPortProperty(env, value, "port", &outLaunchConfig->port) &&
           readOptionalStringProperty(env, value, "token", &outLaunchConfig->token);
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
    size_t argc = 1;
    napi_value args[1];

    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc > 1) {
        throwTypeError(env, "initialize expects zero or one argument");
        return nullptr;
    }

    qodex::threadui::native::LaunchConfig launchConfig;
    if (argc == 1 && !readLaunchConfig(env, args[0], &launchConfig)) {
        return nullptr;
    }

    qodex::threadui::native::initialize(launchConfig);
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

napi_value sendUserInputWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];

    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 1) {
        throwTypeError(env, "sendUserInput expects exactly 1 argument");
        return nullptr;
    }

    size_t textLength = 0;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &textLength) != napi_ok) {
        throwTypeError(env, "sendUserInput expects a string");
        return nullptr;
    }

    std::string text(textLength + 1, '\0');
    size_t copiedLength = 0;
    if (napi_get_value_string_utf8(env, args[0], text.data(), text.size(), &copiedLength) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read sendUserInput text");
        return nullptr;
    }

    text.resize(copiedLength);
    qodex::threadui::native::sendUserInput(text);
    return getUndefined(env);
}

napi_value takePendingItemsWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "takePendingItems expects no arguments");
        return nullptr;
    }

    const std::vector<qodex::threadui::native::DisplayItem> items = qodex::threadui::native::takePendingItems();
    napi_value array = nullptr;
    if (napi_create_array_with_length(env, items.size(), &array) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create pending items array");
        return nullptr;
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        napi_value itemObject = nullptr;
        napi_value kindValue = nullptr;
        napi_value textValue = nullptr;

        if (napi_create_object(env, &itemObject) != napi_ok ||
            napi_create_string_utf8(
                env,
                items[index].kind == qodex::threadui::native::DisplayItemKind::UserMessage ? "user" : "agent",
                NAPI_AUTO_LENGTH,
                &kindValue
            ) != napi_ok ||
            napi_create_string_utf8(env, items[index].text.c_str(), NAPI_AUTO_LENGTH, &textValue) != napi_ok ||
            napi_set_named_property(env, itemObject, "kind", kindValue) != napi_ok ||
            napi_set_named_property(env, itemObject, "text", textValue) != napi_ok ||
            napi_set_element(env, array, index, itemObject) != napi_ok) {
            napi_throw_error(env, nullptr, "Failed to create pending item value");
            return nullptr;
        }
    }

    return array;
}

napi_value takeFatalErrorWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "takeFatalError expects no arguments");
        return nullptr;
    }

    const std::string fatalError = qodex::threadui::native::takeFatalError();
    if (fatalError.empty()) {
        return getUndefined(env);
    }

    napi_value result = nullptr;
    if (napi_create_string_utf8(env, fatalError.c_str(), NAPI_AUTO_LENGTH, &result) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create fatal error string");
        return nullptr;
    }

    return result;
}

napi_value takePendingErrorWrapped(napi_env env, napi_callback_info info) {
    size_t argc = 0;

    if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to read function arguments");
        return nullptr;
    }

    if (argc != 0) {
        throwTypeError(env, "takePendingError expects no arguments");
        return nullptr;
    }

    const std::string pendingError = qodex::threadui::native::takePendingError();
    if (pendingError.empty()) {
        return getUndefined(env);
    }

    napi_value result = nullptr;
    if (napi_create_string_utf8(env, pendingError.c_str(), NAPI_AUTO_LENGTH, &result) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to create pending error string");
        return nullptr;
    }

    return result;
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
        exportFunction(env, exports, "sendUserInput", sendUserInputWrapped) != napi_ok ||
        exportFunction(env, exports, "tick", tickWrapped) != napi_ok ||
        exportFunction(env, exports, "takeFatalError", takeFatalErrorWrapped) != napi_ok ||
        exportFunction(env, exports, "takePendingError", takePendingErrorWrapped) != napi_ok ||
        exportFunction(env, exports, "takePendingItems", takePendingItemsWrapped) != napi_ok ||
        exportFunction(env, exports, "shutdown", shutdownWrapped) != napi_ok) {
        napi_throw_error(env, nullptr, "Failed to export native functions");
        return nullptr;
    }

    return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
