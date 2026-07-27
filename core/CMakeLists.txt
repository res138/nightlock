include(${CMAKE_SOURCE_DIR}/cmake/FindSodium.cmake)

add_library(nightlock-core STATIC
    src/crypto/sodium_backend.cpp
    src/format/serialize.cpp
    src/format/tlv.cpp
    src/generator/generator.cpp
    src/model/group.cpp
    src/secure/secure.cpp
    src/vault/atomic_write.cpp
    src/vault/vault_file.cpp
)

target_include_directories(nightlock-core PUBLIC include)
target_include_directories(nightlock-core PRIVATE src)
target_link_libraries(nightlock-core PRIVATE nightlock::sodium)
