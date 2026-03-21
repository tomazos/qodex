cmake_minimum_required(VERSION 3.24)

project(qodex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)
find_package(KDDockWidgets QUIET)
if(NOT KDDockWidgets_FOUND)
    find_package(KDDockWidgets-qt6 REQUIRED)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BASH_EXECUTABLE bash REQUIRED)

qt_standard_project_setup()
include(CTest)

set(QODEX_SETUP_PYTHON_SCRIPT "${PROJECT_SOURCE_DIR}/setup_python.sh")
set(QODEX_VENV_PYTHON "${PROJECT_SOURCE_DIR}/.venv/bin/python")
set(QODEX_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated")
set(QODEX_PROTOCOL_SCHEMA_ROOT "${QODEX_GENERATED_ROOT}/protocol/schema")
set(QODEX_PROTOCOL_MODEL_ROOT "${QODEX_GENERATED_ROOT}/protocol/model")
set(QODEX_PYTHON_ENV_STAMP "${QODEX_GENERATED_ROOT}/python-env.stamp")

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
    src/main.cpp
    src/app/AppBootstrap.cpp
    src/app/AppBootstrap.h
    src/app/AppConfig.cpp
    src/app/AppConfig.h
    src/app/AppPaths.cpp
    src/app/AppPaths.h
    src/app/SessionController.cpp
    src/app/SessionController.h
    src/codex/AppServerTransport.cpp
    src/codex/AppServerTransport.h
    src/codex/CodexClientBase.cpp
    src/codex/CodexClientBase.h
    src/codex/JsonRpcMessage.h
    src/domain/CodexTypes.h
    src/domain/ThreadStore.cpp
    src/domain/ThreadStore.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ThreadListModel.cpp
    src/ui/ThreadListModel.h
    src/ui/ThreadListPane.cpp
    src/ui/ThreadListPane.h
)

target_include_directories(qodex PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${QODEX_PROTOCOL_MODEL_ROOT}/${QODEX_CODEX_VERSION}/nonexperimental"
)

target_link_libraries(qodex PRIVATE Qt6::Core Qt6::Widgets KDAB::kddockwidgets)
target_compile_definitions(qodex PRIVATE QODEX_APP_VERSION="${PROJECT_VERSION}")

add_dependencies(qodex qodex_protocol_artifacts)
target_sources(qodex PRIVATE
    "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_CPP}"
    "${QODEX_NONEXPERIMENTAL_CODEX_PROTOCOL_H}"
    "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_CPP}"
    "${QODEX_NONEXPERIMENTAL_CODEX_CLIENT_H}"
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
endif()

install(TARGETS qodex RUNTIME DESTINATION bin)
