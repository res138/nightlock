# Locates libsodium and exposes it as the INTERFACE target
# nightlock::sodium. Two supported paths, tried in order:
#
#   1. System install — pkg-config when present, otherwise the
#      well-known Homebrew prefixes (Apple Silicon and Intel). Fast
#      configure, shared library, updated by brew.
#   2. Vendored — FetchContent of a pinned libsodium-cmake wrapper,
#      built statically inside the tree. Needs network on the first
#      configure, but is reproducible and self-contained (future
#      Windows/Linux ports, .app bundling).
#
# NIGHTLOCK_FORCE_VENDORED_SODIUM=ON skips the system lookup.

if(TARGET nightlock::sodium)
    return()
endif()

add_library(nightlock-sodium INTERFACE)
add_library(nightlock::sodium ALIAS nightlock-sodium)

set(_nightlock_sodium_found FALSE)

if(NOT NIGHTLOCK_FORCE_VENDORED_SODIUM)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM QUIET IMPORTED_TARGET libsodium>=1.0.18)
        if(SODIUM_FOUND)
            target_link_libraries(nightlock-sodium INTERFACE PkgConfig::SODIUM)
            set(_nightlock_sodium_found TRUE)
            message(STATUS "nightlock: system libsodium ${SODIUM_VERSION} (pkg-config)")
        endif()
    endif()

    if(NOT _nightlock_sodium_found)
        find_path(SODIUM_INCLUDE_DIR sodium.h
            HINTS /opt/homebrew/opt/libsodium/include /usr/local/opt/libsodium/include)
        find_library(SODIUM_LIBRARY sodium
            HINTS /opt/homebrew/opt/libsodium/lib /usr/local/opt/libsodium/lib)
        if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
            target_include_directories(nightlock-sodium INTERFACE "${SODIUM_INCLUDE_DIR}")
            target_link_libraries(nightlock-sodium INTERFACE "${SODIUM_LIBRARY}")
            set(_nightlock_sodium_found TRUE)
            message(STATUS "nightlock: system libsodium at ${SODIUM_LIBRARY}")
        endif()
    endif()
endif()

if(NOT _nightlock_sodium_found)
    include(FetchContent)
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(sodium
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d)
    FetchContent_MakeAvailable(sodium)
    target_link_libraries(nightlock-sodium INTERFACE sodium)
    message(STATUS "nightlock: vendored libsodium (FetchContent)")
endif()
