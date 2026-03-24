cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QODEX_VERSION LIMIT_COUNT 1)
if(NOT QODEX_VERSION)
    message(FATAL_ERROR "Unable to read qodex version from VERSION")
endif()

project(qodex VERSION "${QODEX_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets Test)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(KDDockWidgets QUIET)
if(NOT KDDockWidgets_FOUND)
    find_package(KDDockWidgets-qt6 REQUIRED)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BASH_EXECUTABLE bash REQUIRED)
find_program(NPM_EXECUTABLE npm REQUIRED)
find_path(QODEX_ASIO_INCLUDE_DIR NAMES asio.hpp REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(QodexElectron)
qt_standard_project_setup()
include(CTest)

set(QODEX_SETUP_PYTHON_SCRIPT "${PROJECT_SOURCE_DIR}/setup_python.sh")
set(QODEX_VENV_PYTHON "${PROJECT_SOURCE_DIR}/.venv/bin/python")
set(QODEX_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated")
set(QODEX_PROTOCOL_SCHEMA_ROOT "${QODEX_GENERATED_ROOT}/protocol/schema")
set(QODEX_PROTOCOL_MODEL_ROOT "${QODEX_GENERATED_ROOT}/protocol/model")
set(QODEX_PYTHON_ENV_STAMP "${QODEX_GENERATED_ROOT}/python-env.stamp")
set(QODEX_THREAD_UI_SOURCE_DIR "${PROJECT_SOURCE_DIR}/frontend/thread-ui")
set(QODEX_THREAD_UI_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/runtime/thread-ui")
set(QODEX_THREAD_UI_APP_DIR "${QODEX_THREAD_UI_RUNTIME_ROOT}/app")
set(QODEX_THREAD_UI_NATIVE_DIR "${QODEX_THREAD_UI_RUNTIME_ROOT}/native")
set(QODEX_THREAD_UI_INSTALLED_RELATIVE_APP_DIR "../lib/qodex/thread-ui/app")
set(QODEX_THREAD_UI_INSTALL_APP_DIR "lib/qodex/thread-ui/app")
set(QODEX_THREAD_UI_INSTALL_NATIVE_DIR "lib/qodex/thread-ui/native")
set(QODEX_DESKTOP_FILE_ID "com.tomazos.qodex")
set(QODEX_DESKTOP_ENTRY_FILE "${PROJECT_SOURCE_DIR}/packaging/qodex.desktop")
set(QODEX_METAINFO_FILE "${PROJECT_SOURCE_DIR}/packaging/qodex.metainfo.xml")
set(QODEX_ICON_SIZES 16 20 22 24 32 48 64 128 256 512)

set(QODEX_THREAD_UI_SOURCE_FILES
    "${QODEX_THREAD_UI_SOURCE_DIR}/package.json"
    "${QODEX_THREAD_UI_SOURCE_DIR}/package-lock.json"
    "${QODEX_THREAD_UI_SOURCE_DIR}/main.js"
    "${QODEX_THREAD_UI_SOURCE_DIR}/init.js"
    "${QODEX_THREAD_UI_SOURCE_DIR}/index.html"
    "${QODEX_THREAD_UI_SOURCE_DIR}/styles.css"
    "${QODEX_THREAD_UI_SOURCE_DIR}/scripts/start-electron.js"
    "${QODEX_THREAD_UI_SOURCE_DIR}/test-links/local-page.html"
)

qodex_add_staged_thread_ui(qodex_thread_ui_assets
    SOURCE_DIR "${QODEX_THREAD_UI_SOURCE_DIR}"
    APP_DIR "${QODEX_THREAD_UI_APP_DIR}"
    NPM_EXECUTABLE "${NPM_EXECUTABLE}"
    SOURCE_FILES ${QODEX_THREAD_UI_SOURCE_FILES}
)

qodex_add_napi_module(qodex_thread_ui_native
    OUTPUT_DIRECTORY "${QODEX_THREAD_UI_NATIVE_DIR}"
    OUTPUT_NAME "qodex_thread_ui"
    SOURCES
        "${PROJECT_SOURCE_DIR}/src/threadui_native/NativeAdd.cpp"
        "${PROJECT_SOURCE_DIR}/src/threadui_native/ThreadUiAddon.cpp"
        "${PROJECT_SOURCE_DIR}/src/threadui_native/ThreadUiEngine.cpp"
)
target_include_directories(qodex_thread_ui_native PRIVATE "${QODEX_ASIO_INCLUDE_DIR}")
target_compile_definitions(qodex_thread_ui_native PRIVATE ASIO_STANDALONE)
target_link_libraries(qodex_thread_ui_native PRIVATE Threads::Threads)

add_custom_target(qodex_thread_ui
    DEPENDS
        qodex_thread_ui_assets
        qodex_thread_ui_native
)

set(QODEX_PROTOCOL_SCHEMA_SOURCES
    "${PROJECT_SOURCE_DIR}/scripts/generate_protocol_schema.py"
    "${PROJECT_SOURCE_DIR}/scripts/export_protocol_model_xml.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/__init__.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/model.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/parser.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/protocol.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.py"
    "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.xsd"
)

set(QODEX_SERVICE_TRANSLATION_SOURCES
    "${PROJECT_SOURCE_DIR}/scripts/translate_service.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/__init__.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/model.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/parser.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/__init__.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/cpp.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/html.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/null.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/qt.py"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/cpp/header.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/card.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/catalog.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/catalog_line.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/document.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/header.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/message_group.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/html/section.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/qt/CodexProtocol.h.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/qt/CodexProtocol.cpp.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/qt/CodexClient.h.j2"
    "${PROJECT_SOURCE_DIR}/scripts/service_translation/generators/templates/qt/CodexClient.cpp.j2"
)

add_custom_command(
    OUTPUT "${QODEX_PYTHON_ENV_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${QODEX_GENERATED_ROOT}"
    COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${QODEX_PYTHON_ENV_STAMP}"
    DEPENDS
        "${QODEX_SETUP_PYTHON_SCRIPT}"
        "${PROJECT_SOURCE_DIR}/requirements.txt"
    VERBATIM
)

add_custom_target(qodex_python_env
    DEPENDS "${QODEX_PYTHON_ENV_STAMP}"
)

function(
    qodex_add_service_variant
    variant
    schema_marker
    output_xml
    output_html
    output_h
    output_protocol_h
    output_protocol_cpp
    output_client_h
    output_client_cpp
)
    add_custom_command(
        OUTPUT "${output_xml}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${QODEX_PROTOCOL_MODEL_ROOT}>/${QODEX_CODEX_VERSION}/${variant}"
        COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
        COMMAND "${QODEX_VENV_PYTHON}"
                "${PROJECT_SOURCE_DIR}/scripts/export_protocol_model_xml.py"
                "${QODEX_PROTOCOL_SCHEMA_ROOT}/${QODEX_CODEX_VERSION}/${variant}"
                --out "${output_xml}"
                --xsd "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.xsd"
        DEPENDS
            "${QODEX_PYTHON_ENV_STAMP}"
            "${schema_marker}"
            ${QODEX_PROTOCOL_SCHEMA_SOURCES}
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${output_html}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${QODEX_PROTOCOL_MODEL_ROOT}>/${QODEX_CODEX_VERSION}/${variant}"
        COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
        COMMAND "${QODEX_VENV_PYTHON}"
                "${PROJECT_SOURCE_DIR}/scripts/translate_service.py"
                "${output_xml}"
                html
                "${output_html}"
                --xsd "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.xsd"
        DEPENDS
            "${QODEX_PYTHON_ENV_STAMP}"
            "${output_xml}"
            ${QODEX_PROTOCOL_SCHEMA_SOURCES}
            ${QODEX_SERVICE_TRANSLATION_SOURCES}
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${output_h}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${QODEX_PROTOCOL_MODEL_ROOT}>/${QODEX_CODEX_VERSION}/${variant}"
        COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
        COMMAND "${QODEX_VENV_PYTHON}"
                "${PROJECT_SOURCE_DIR}/scripts/translate_service.py"
                "${output_xml}"
                cpp
                "${output_h}"
                --xsd "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.xsd"
        DEPENDS
            "${QODEX_PYTHON_ENV_STAMP}"
            "${output_xml}"
            ${QODEX_PROTOCOL_SCHEMA_SOURCES}
            ${QODEX_SERVICE_TRANSLATION_SOURCES}
        VERBATIM
    )

    add_custom_command(
        OUTPUT
            "${output_protocol_h}"
            "${output_protocol_cpp}"
            "${output_client_h}"
            "${output_client_cpp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${QODEX_PROTOCOL_MODEL_ROOT}>/${QODEX_CODEX_VERSION}/${variant}"
        COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
        COMMAND "${QODEX_VENV_PYTHON}"
                "${PROJECT_SOURCE_DIR}/scripts/translate_service.py"
                "${output_xml}"
                qt
                "$<SHELL_PATH:${QODEX_PROTOCOL_MODEL_ROOT}>/${QODEX_CODEX_VERSION}/${variant}"
                --xsd "${PROJECT_SOURCE_DIR}/scripts/protocol_schema/service_ir.xsd"
        DEPENDS
            "${QODEX_PYTHON_ENV_STAMP}"
            "${output_xml}"
            ${QODEX_PROTOCOL_SCHEMA_SOURCES}
            ${QODEX_SERVICE_TRANSLATION_SOURCES}
        VERBATIM
    )
endfunction()

find_program(CODEX_EXECUTABLE codex)
if(CODEX_EXECUTABLE)
    execute_process(
        COMMAND "${CODEX_EXECUTABLE}" --version
        OUTPUT_VARIABLE QODEX_CODEX_VERSION_OUTPUT
        ERROR_VARIABLE QODEX_CODEX_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE QODEX_CODEX_VERSION_RESULT
    )
    if(QODEX_CODEX_VERSION_RESULT EQUAL 0)
        if(QODEX_CODEX_VERSION_OUTPUT STREQUAL "")
            set(QODEX_CODEX_VERSION_OUTPUT "${QODEX_CODEX_VERSION_ERROR}")
        endif()
        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+([-.+_A-Za-z0-9]+)?" QODEX_CODEX_VERSION "${QODEX_CODEX_VERSION_OUTPUT}")
    endif()
endif()

if(CODEX_EXECUTABLE AND QODEX_CODEX_VERSION)
    set(QODEX_NONEXPERIMENTAL_SCHEMA_MARKER
        "${QODEX_PROTOCOL_SCHEMA_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/codex_app_server_protocol.schemas.json"
    )
    set(QODEX_EXPERIMENTAL_SCHEMA_MARKER
        "${QODEX_PROTOCOL_SCHEMA_ROOT}/${QODEX_CODEX_VERSION}/experimental/codex_app_server_protocol.schemas.json"
    )

    add_custom_command(
        OUTPUT
            "${QODEX_NONEXPERIMENTAL_SCHEMA_MARKER}"
            "${QODEX_EXPERIMENTAL_SCHEMA_MARKER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${QODEX_PROTOCOL_SCHEMA_ROOT}"
        COMMAND "${BASH_EXECUTABLE}" "${QODEX_SETUP_PYTHON_SCRIPT}"
        COMMAND "${QODEX_VENV_PYTHON}"
                "${PROJECT_SOURCE_DIR}/scripts/generate_protocol_schema.py"
                --codex "${CODEX_EXECUTABLE}"
                --schema-root "${QODEX_PROTOCOL_SCHEMA_ROOT}"
                --version "${QODEX_CODEX_VERSION}"
        DEPENDS
            "${QODEX_PYTHON_ENV_STAMP}"
            "${PROJECT_SOURCE_DIR}/scripts/generate_protocol_schema.py"
        VERBATIM
    )

    set(QODEX_NONEXPERIMENTAL_SERVICE_XML
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/service.xml"
    )
    set(QODEX_NONEXPERIMENTAL_SERVICE_HTML
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/service.html"
    )
    set(QODEX_NONEXPERIMENTAL_SERVICE_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/service.h"
    )
    set(QODEX_NONEXPERIMENTAL_CODEX_CLIENT_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/CodexClient.h"
    )
    set(QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/CodexProtocol.h"
    )
    set(QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_CPP
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/CodexProtocol.cpp"
    )
    set(QODEX_NONEXPERIMENTAL_CODEX_CLIENT_CPP
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental/CodexClient.cpp"
    )
    set(QODEX_EXPERIMENTAL_SERVICE_XML
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/service.xml"
    )
    set(QODEX_EXPERIMENTAL_SERVICE_HTML
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/service.html"
    )
    set(QODEX_EXPERIMENTAL_SERVICE_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/service.h"
    )
    set(QODEX_EXPERIMENTAL_CODEX_CLIENT_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/CodexClient.h"
    )
    set(QODEX_EXPERIMENTAL_CODEX_PROTOCOL_H
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/CodexProtocol.h"
    )
    set(QODEX_EXPERIMENTAL_CODEX_PROTOCOL_CPP
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/CodexProtocol.cpp"
    )
    set(QODEX_EXPERIMENTAL_CODEX_CLIENT_CPP
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental/CodexClient.cpp"
    )

    qodex_add_service_variant(
        nonexperimental
        "${QODEX_NONEXPERIMENTAL_SCHEMA_MARKER}"
        "${QODEX_NONEXPERIMENTAL_SERVICE_XML}"
        "${QODEX_NONEXPERIMENTAL_SERVICE_HTML}"
        "${QODEX_NONEXPERIMENTAL_SERVICE_H}"
        "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_H}"
        "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_CPP}"
        "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_H}"
        "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_CPP}"
    )
    qodex_add_service_variant(
        experimental
        "${QODEX_EXPERIMENTAL_SCHEMA_MARKER}"
        "${QODEX_EXPERIMENTAL_SERVICE_XML}"
        "${QODEX_EXPERIMENTAL_SERVICE_HTML}"
        "${QODEX_EXPERIMENTAL_SERVICE_H}"
        "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_H}"
        "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_CPP}"
        "${QODEX_EXPERIMENTAL_CODEX_CLIENT_H}"
        "${QODEX_EXPERIMENTAL_CODEX_CLIENT_CPP}"
    )

    add_custom_target(qodex_protocol_artifacts
        DEPENDS
            "${QODEX_NONEXPERIMENTAL_SERVICE_XML}"
            "${QODEX_NONEXPERIMENTAL_SERVICE_HTML}"
            "${QODEX_NONEXPERIMENTAL_SERVICE_H}"
            "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_H}"
            "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_CPP}"
            "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_H}"
            "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_CPP}"
            "${QODEX_EXPERIMENTAL_SERVICE_XML}"
            "${QODEX_EXPERIMENTAL_SERVICE_HTML}"
            "${QODEX_EXPERIMENTAL_SERVICE_H}"
            "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_H}"
            "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_CPP}"
            "${QODEX_EXPERIMENTAL_CODEX_CLIENT_H}"
            "${QODEX_EXPERIMENTAL_CODEX_CLIENT_CPP}"
    )
else()
    message(STATUS "codex executable not found or version unreadable; skipping protocol artifact generation targets.")
endif()

if(NOT TARGET qodex_protocol_artifacts)
    message(FATAL_ERROR "qodex requires the codex executable to generate protocol artifacts at configure time.")
endif()

qt_add_executable(qodex
    resources/qodex.qrc
    src/main.cpp
    src/app/AppBootstrap.cpp
    src/app/AppBootstrap.h
    src/app/AppConfig.cpp
    src/app/AppConfig.h
    src/app/AppPaths.cpp
    src/app/AppPaths.h
    src/app/SessionController.cpp
    src/app/SessionController.h
    src/app/SingleInstanceManager.cpp
    src/app/SingleInstanceManager.h
    src/app/ThreadUiProcessManager.cpp
    src/app/ThreadUiProcessManager.h
    src/codex/AppServerTransport.cpp
    src/codex/AppServerTransport.h
    src/codex/CodexClientBase.cpp
    src/codex/CodexClientBase.h
    src/codex/JsonRpcMessage.h
    src/codex/TrafficLogger.cpp
    src/codex/TrafficLogger.h
    src/domain/CodexTypes.h
    src/domain/ThreadStore.cpp
    src/domain/ThreadStore.h
    src/storage/DatabaseManager.cpp
    src/storage/DatabaseManager.h
    src/storage/Migration.h
    src/storage/MigrationRunner.cpp
    src/storage/MigrationRunner.h
    src/threadui/ThreadUiIpcServer.cpp
    src/threadui/ThreadUiIpcServer.h
    src/ui/ApiLogModel.cpp
    src/ui/ApiLogModel.h
    src/ui/ApiLogPane.cpp
    src/ui/ApiLogPane.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ProgressSplashScreen.cpp
    src/ui/ProgressSplashScreen.h
    src/ui/ThreadListModel.cpp
    src/ui/ThreadListModel.h
    src/ui/ThreadListPane.cpp
    src/ui/ThreadListPane.h
)

target_include_directories(qodex PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental"
)

target_link_libraries(qodex PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets KDAB::kddockwidgets SQLite::SQLite3)
target_compile_definitions(qodex PRIVATE
    QODEX_APP_VERSION="${PROJECT_VERSION}"
    QODEX_THREAD_UI_BUILD_APP_DIR="${QODEX_THREAD_UI_APP_DIR}"
    QODEX_THREAD_UI_INSTALLED_RELATIVE_APP_DIR="${QODEX_THREAD_UI_INSTALLED_RELATIVE_APP_DIR}"
)

add_dependencies(qodex qodex_protocol_artifacts)
add_dependencies(qodex qodex_thread_ui)
target_sources(qodex PRIVATE
    "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_CPP}"
    "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_H}"
    "${QODEX_EXPERIMENTAL_CODEX_CLIENT_CPP}"
    "${QODEX_EXPERIMENTAL_CODEX_CLIENT_H}"
)

if(BUILD_TESTING)
    qt_add_executable(AppServerTransportTest
        tests/codex/AppServerTransportTest.cpp
        src/codex/AppServerTransport.cpp
        src/codex/AppServerTransport.h
        src/codex/JsonRpcMessage.h
    )

    target_include_directories(AppServerTransportTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(AppServerTransportTest PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME AppServerTransportTest COMMAND AppServerTransportTest)

    qt_add_executable(TrafficLoggerTest
        resources/qodex.qrc
        tests/codex/TrafficLoggerTest.cpp
        src/codex/AppServerTransport.cpp
        src/codex/AppServerTransport.h
        src/codex/JsonRpcMessage.h
        src/codex/TrafficLogger.cpp
        src/codex/TrafficLogger.h
        src/storage/DatabaseManager.cpp
        src/storage/DatabaseManager.h
        src/storage/Migration.h
        src/storage/MigrationRunner.cpp
        src/storage/MigrationRunner.h
    )

    target_include_directories(TrafficLoggerTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(TrafficLoggerTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test SQLite::SQLite3)

    add_test(NAME TrafficLoggerTest COMMAND TrafficLoggerTest)

    qt_add_executable(CodexClientGeneratedTest
        tests/codex/CodexClientGeneratedTest.cpp
        src/codex/AppServerTransport.cpp
        src/codex/AppServerTransport.h
        src/codex/CodexClientBase.cpp
        src/codex/CodexClientBase.h
        src/codex/JsonRpcMessage.h
        "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_CPP}"
        "${QODEX_EXPERIMENTAL_CODEX_PROTOCOL_H}"
        "${QODEX_EXPERIMENTAL_CODEX_CLIENT_CPP}"
        "${QODEX_EXPERIMENTAL_CODEX_CLIENT_H}"
    )

    add_dependencies(CodexClientGeneratedTest qodex_protocol_artifacts)

    target_include_directories(CodexClientGeneratedTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/experimental"
    )

    target_link_libraries(CodexClientGeneratedTest PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME CodexClientGeneratedTest COMMAND CodexClientGeneratedTest)

    qt_add_executable(ThreadStoreTest
        tests/domain/ThreadStoreTest.cpp
        src/domain/CodexTypes.h
        src/domain/ThreadStore.cpp
        src/domain/ThreadStore.h
    )

    target_include_directories(ThreadStoreTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(ThreadStoreTest PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME ThreadStoreTest COMMAND ThreadStoreTest)

    qt_add_executable(SingleInstanceManagerTest
        tests/app/SingleInstanceManagerTest.cpp
        src/app/SingleInstanceManager.cpp
        src/app/SingleInstanceManager.h
    )

    target_include_directories(SingleInstanceManagerTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(SingleInstanceManagerTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    add_test(NAME SingleInstanceManagerTest COMMAND SingleInstanceManagerTest)

    qt_add_executable(ThreadUiIpcServerTest
        tests/threadui/ThreadUiIpcServerTest.cpp
        src/threadui/ThreadUiIpcServer.cpp
        src/threadui/ThreadUiIpcServer.h
    )

    target_include_directories(ThreadUiIpcServerTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(ThreadUiIpcServerTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    add_test(NAME ThreadUiIpcServerTest COMMAND ThreadUiIpcServerTest)

    qt_add_executable(ThreadUiEngineConnectionTest
        tests/threadui/ThreadUiEngineConnectionTest.cpp
        src/threadui/ThreadUiIpcServer.cpp
        src/threadui/ThreadUiIpcServer.h
        src/threadui_native/ThreadUiEngine.cpp
        src/threadui_native/ThreadUiEngine.h
    )

    target_include_directories(ThreadUiEngineConnectionTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${QODEX_ASIO_INCLUDE_DIR}"
    )

    target_compile_definitions(ThreadUiEngineConnectionTest PRIVATE ASIO_STANDALONE)

    target_link_libraries(ThreadUiEngineConnectionTest PRIVATE Qt6::Core Qt6::Network Qt6::Test Threads::Threads)

    add_test(NAME ThreadUiEngineConnectionTest COMMAND ThreadUiEngineConnectionTest)

    qt_add_executable(MigrationRunnerTest
        resources/qodex.qrc
        tests/storage/MigrationRunnerTest.cpp
        src/storage/Migration.h
        src/storage/MigrationRunner.cpp
        src/storage/MigrationRunner.h
    )

    target_include_directories(MigrationRunnerTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(MigrationRunnerTest PRIVATE Qt6::Core Qt6::Test SQLite::SQLite3)

    add_test(NAME MigrationRunnerTest COMMAND MigrationRunnerTest)

    qt_add_executable(DatabaseManagerTest
        resources/qodex.qrc
        tests/storage/DatabaseManagerTest.cpp
        src/storage/DatabaseManager.cpp
        src/storage/DatabaseManager.h
        src/storage/Migration.h
        src/storage/MigrationRunner.cpp
        src/storage/MigrationRunner.h
    )

    target_include_directories(DatabaseManagerTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(DatabaseManagerTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test SQLite::SQLite3)

    add_test(NAME DatabaseManagerTest COMMAND DatabaseManagerTest)

    qt_add_executable(ThreadListModelTest
        resources/qodex.qrc
        tests/ui/ThreadListModelTest.cpp
        src/domain/CodexTypes.h
        src/domain/ThreadStore.cpp
        src/domain/ThreadStore.h
        src/ui/ThreadListModel.cpp
        src/ui/ThreadListModel.h
    )

    target_include_directories(ThreadListModelTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(ThreadListModelTest PRIVATE Qt6::Core Qt6::Test Qt6::Widgets)

    add_test(NAME ThreadListModelTest COMMAND ThreadListModelTest)

    qt_add_executable(ApiLogModelTest
        resources/qodex.qrc
        tests/ui/ApiLogModelTest.cpp
        src/storage/DatabaseManager.cpp
        src/storage/DatabaseManager.h
        src/storage/Migration.h
        src/storage/MigrationRunner.cpp
        src/storage/MigrationRunner.h
        src/ui/ApiLogModel.cpp
        src/ui/ApiLogModel.h
    )

    target_include_directories(ApiLogModelTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(ApiLogModelTest PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test SQLite::SQLite3)

    add_test(NAME ApiLogModelTest COMMAND ApiLogModelTest)

endif()

install(TARGETS qodex RUNTIME DESTINATION bin)
install(TARGETS qodex_thread_ui_native LIBRARY DESTINATION "${QODEX_THREAD_UI_INSTALL_NATIVE_DIR}")
install(
    DIRECTORY "${QODEX_THREAD_UI_APP_DIR}/"
    DESTINATION "${QODEX_THREAD_UI_INSTALL_APP_DIR}"
    USE_SOURCE_PERMISSIONS
    PATTERN ".qodex-*.stamp" EXCLUDE
)
install(FILES "${QODEX_DESKTOP_ENTRY_FILE}" DESTINATION share/applications RENAME "${QODEX_DESKTOP_FILE_ID}.desktop")
install(FILES "${QODEX_METAINFO_FILE}" DESTINATION share/metainfo RENAME "${QODEX_DESKTOP_FILE_ID}.metainfo.xml")
install(FILES "${PROJECT_SOURCE_DIR}/images/qodex-icon-512x512.png" DESTINATION share/pixmaps RENAME qodex.png)
foreach(icon_size IN LISTS QODEX_ICON_SIZES)
    install(
        FILES "${PROJECT_SOURCE_DIR}/images/qodex-icon-${icon_size}x${icon_size}.png"
        DESTINATION "share/icons/hicolor/${icon_size}x${icon_size}/apps"
        RENAME qodex.png
    )
endforeach()
