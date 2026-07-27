#include <cstdio>
#include <string>
#include <vector>

#include "commands.hpp"

int main(int argc, char* argv[]) {
    cli::GlobalOptions global;
    std::string command;
    std::vector<std::string> args;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (command.empty()) {
            if (arg == "-h" || arg == "--help") {
                cli::printUsage();
                return cli::kExitOk;
            }
            if (arg == "-V" || arg == "--version") {
                std::puts("nightlock " NIGHTLOCK_VERSION);
                return cli::kExitOk;
            }
            if (arg == "-f" || arg == "--file") {
                if (i + 1 >= argc) {
                    std::fputs("nightlock: --file needs a path\n", stderr);
                    return cli::kExitUsage;
                }
                global.vaultPath = argv[++i];
                continue;
            }
            if (arg == "--password-stdin") {
                global.passwordStdin = true;
                continue;
            }
            command = arg;
            continue;
        }
        args.push_back(arg);
    }

    if (command.empty()) {
        cli::printUsage();
        return cli::kExitUsage;
    }
    if (global.vaultPath.empty())
        global.vaultPath = cli::defaultVaultPath();
    return cli::runCommand(command, global, args);
}
