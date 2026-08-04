add_executable(nightlock-tests
    test_main.cpp
    test_crypto.cpp
    test_generator.cpp
    test_group.cpp
    test_secure.cpp
    test_serialize.cpp
    test_tlv.cpp
    test_vault_file.cpp
)

target_link_libraries(nightlock-tests PRIVATE nightlock-core)
# doctest's lightweight std forward declarations are incompatible with
# MSVC's string_view stream operators. Use the real standard headers in
# every test translation unit instead of relying on incidental includes.
target_compile_definitions(nightlock-tests PRIVATE
    DOCTEST_CONFIG_USE_STD_HEADERS
)
# Tests reach internal headers (TLV reader, serializer) directly.
target_include_directories(nightlock-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/core/src
)

add_test(NAME core COMMAND nightlock-tests)

if(NIGHTLOCK_BUILD_CLI)
    add_test(NAME cli_smoke
        COMMAND ${CMAKE_COMMAND}
            -DNIGHTLOCK_CLI=$<TARGET_FILE:nightlock-cli>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)
endif()
