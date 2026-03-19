cmake_minimum_required(VERSION 3.24)

project(qodex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(qodex
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)

target_link_libraries(qodex PRIVATE Qt6::Widgets)

install(TARGETS qodex RUNTIME DESTINATION bin)
