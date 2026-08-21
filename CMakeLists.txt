cmake_minimum_required(VERSION 3.21)

# Keep release bundles runnable on the full Qt 6.10 macOS baseline instead of
# silently inheriting the build runner's SDK version.  This also prevents an
# empty LSMinimumSystemVersion value in the generated Info.plist.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
        "Minimum supported macOS version" FORCE)
endif()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" NIGHTLOCK_VERSION LIMIT_COUNT 1)
if(NOT NIGHTLOCK_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version such as 1.2.0")
endif()

project(nightlock VERSION "${NIGHTLOCK_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NIGHTLOCK_BUILD_DESKTOP "Build the Qt desktop app" ON)
option(NIGHTLOCK_BUILD_CLI "Build the command-line app" ON)
option(NIGHTLOCK_BUILD_TESTS "Build unit tests" ON)
option(NIGHTLOCK_FORCE_VENDORED_SODIUM "Skip the system libsodium lookup" OFF)

add_subdirectory(core)
if(NIGHTLOCK_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()
if(NIGHTLOCK_BUILD_DESKTOP)
    add_subdirectory(apps/desktop)
endif()
if(NIGHTLOCK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
