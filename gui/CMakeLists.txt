cmake_minimum_required(VERSION 3.16)

project(UR44Control VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTORCC ON)

# Find the Qt Widgets library
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

qt_standard_project_setup()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
