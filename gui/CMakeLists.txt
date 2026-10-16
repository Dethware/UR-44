cmake_minimum_required(VERSION 3.16)

project(UR44Control VERSION 0.1.0 LANGUAGES CXX)
# The date of this version, for the AppStream release entry; update it together with the version
set(UR44_RELEASE_DATE "2026-10-16")
set(UR44_APP_ID "org.dethware.ur44control")

include(GNUInstallDirs)
option(BUILD_TESTING "Build the tests (needs Qt Test)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTORCC ON)

# Find the Qt Widgets library
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_subdirectory(src)
# Desktop entry, icon, AppStream metadata
add_subdirectory(data)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()

# License and attribution for packages
install(FILES ../LICENSE ../NOTICE ../README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
