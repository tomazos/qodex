include_guard(GLOBAL)

function(qodex_find_node_api_include_dir out_var)
    find_path(qodex_node_api_include_dir
        NAMES node_api.h
        PATH_SUFFIXES node nodejs/src include/node
        PATHS
            /usr/include
            /usr/local/include
            "$ENV{ProgramFiles}/nodejs/include/node"
    )

    if(NOT qodex_node_api_include_dir)
        message(FATAL_ERROR "Unable to find node_api.h. Install libnode-dev or provide Node headers.")
    endif()

    set(${out_var} "${qodex_node_api_include_dir}" PARENT_SCOPE)
endfunction()

function(qodex_set_target_output_directory target output_directory)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${output_directory}"
        RUNTIME_OUTPUT_DIRECTORY "${output_directory}"
        ARCHIVE_OUTPUT_DIRECTORY "${output_directory}"
    )

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(config ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER "${config}" config_upper)
            set_target_properties(${target} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${output_directory}"
                RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${output_directory}"
                ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${output_directory}"
            )
        endforeach()
    endif()
endfunction()

function(qodex_add_napi_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIRECTORY;OUTPUT_NAME" "SOURCES" ${ARGN})

    if(NOT ARG_OUTPUT_NAME)
        message(FATAL_ERROR "qodex_add_napi_module requires OUTPUT_NAME")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "qodex_add_napi_module requires SOURCES")
    endif()

    qodex_find_node_api_include_dir(qodex_node_api_include_dir)

    add_library(${target} MODULE ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${qodex_node_api_include_dir}" "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(${target} PRIVATE NAPI_VERSION=8 NODE_GYP_MODULE_NAME=${ARG_OUTPUT_NAME})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${ARG_OUTPUT_NAME}"
        PREFIX ""
        SUFFIX ".node"
    )

    if(ARG_OUTPUT_DIRECTORY)
        qodex_set_target_output_directory(${target} "${ARG_OUTPUT_DIRECTORY}")
    endif()

    if(APPLE)
        target_link_options(${target} PRIVATE "-undefined" "dynamic_lookup")
    elseif(WIN32)
        find_library(qodex_node_import_library
            NAMES node libnode
            PATH_SUFFIXES lib
            PATHS
                "$ENV{ProgramFiles}/nodejs"
        )

        if(NOT qodex_node_import_library)
            message(FATAL_ERROR "Unable to find node.lib/libnode.lib for Windows N-API addon builds.")
        endif()

        target_link_libraries(${target} PRIVATE "${qodex_node_import_library}")
    endif()
endfunction()

function(qodex_add_staged_thread_ui target)
    cmake_parse_arguments(ARG "" "SOURCE_DIR;APP_DIR;NPM_EXECUTABLE" "SOURCE_FILES" ${ARGN})

    if(NOT ARG_SOURCE_DIR OR NOT ARG_APP_DIR OR NOT ARG_NPM_EXECUTABLE)
        message(FATAL_ERROR "qodex_add_staged_thread_ui requires SOURCE_DIR, APP_DIR, and NPM_EXECUTABLE")
    endif()

    set(qodex_stage_stamp "${ARG_APP_DIR}/.qodex-stage.stamp")
    set(qodex_npm_stamp "${ARG_APP_DIR}/.qodex-npm.stamp")

    add_custom_command(
        OUTPUT "${qodex_stage_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARG_APP_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_APP_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ARG_SOURCE_DIR}" "${ARG_APP_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${qodex_stage_stamp}"
        DEPENDS ${ARG_SOURCE_FILES}
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${qodex_npm_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "npm_config_audit=false"
                "npm_config_fund=false"
                "${ARG_NPM_EXECUTABLE}" ci --no-audit --no-fund
        COMMAND "${CMAKE_COMMAND}" -E touch "${qodex_npm_stamp}"
        WORKING_DIRECTORY "${ARG_APP_DIR}"
        DEPENDS "${qodex_stage_stamp}"
        VERBATIM
    )

    add_custom_target(${target}
        DEPENDS
            "${qodex_stage_stamp}"
            "${qodex_npm_stamp}"
    )
endfunction()
