cmake_minimum_required(VERSION 3.16)

# The version and its release date (for the AppStream release entry) live in the plain files VERSION and
# RELEASE_DATE at the top of the repository, which the release script writes
set(UR44_TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
file(STRINGS ${UR44_TOP_DIR}/VERSION UR44_VERSION LIMIT_COUNT 1)
file(STRINGS ${UR44_TOP_DIR}/RELEASE_DATE UR44_RELEASE_DATE LIMIT_COUNT 1)
string(STRIP "${UR44_VERSION}" UR44_VERSION)
string(STRIP "${UR44_RELEASE_DATE}" UR44_RELEASE_DATE)
if(NOT UR44_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be X.Y.Z, not '${UR44_VERSION}'")
endif()
if(NOT UR44_RELEASE_DATE MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR "RELEASE_DATE must be YYYY-MM-DD, not '${UR44_RELEASE_DATE}'")
endif()
# reconfigure when they change
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UR44_TOP_DIR}/VERSION ${UR44_TOP_DIR}/RELEASE_DATE)

project(UR44Control VERSION ${UR44_VERSION} LANGUAGES CXX)
set(UR44_APP_ID "org.dethware.ur44control")

include(GNUInstallDirs)
option(BUILD_TESTING "Build the tests (needs Qt Test)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTORCC ON)

# Find the Qt Widgets library
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

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
