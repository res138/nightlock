#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cli {

// Exit codes, also asserted by tests/cli_smoke.cmake.
inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 1;
inline constexpr int kExitPassword = 2;
inline constexpr int kExitIo = 3;

struct GlobalOptions {
    std::filesystem::path vaultPath;  // resolved: -f > $NIGHTLOCK_VAULT > default
    bool passwordStdin = false;
};

// -f/--file > $NIGHTLOCK_VAULT > the platform default (shared with the
// desktop app).
std::filesystem::path defaultVaultPath();

int runCommand(const std::string& command, const GlobalOptions& global,
               const std::vector<std::string>& args);

void printUsage();

}  // namespace cli
