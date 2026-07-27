add_executable(nightlock-cli
    src/commands.cpp
    src/lookup.cpp
    src/main.cpp
    src/termio.cpp
)

target_link_libraries(nightlock-cli PRIVATE nightlock-core)

target_compile_definitions(nightlock-cli PRIVATE
    NIGHTLOCK_VERSION="${PROJECT_VERSION}")

# Same user-facing name as the desktop binary; they live in different
# build directories, so no clash until an install()/packaging step
# exists (at which point the desktop app becomes a .app bundle).
set_target_properties(nightlock-cli PROPERTIES OUTPUT_NAME nightlock)
