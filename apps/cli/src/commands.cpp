#include "commands.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string_view>

#include <nightlock/generator.hpp>
#include <nightlock/vault_file.hpp>

#include "lookup.hpp"
#include "termio.hpp"

namespace cli {

namespace {

using nightlock::VaultError;
using nightlock::VaultFile;

int fail(const char* message) {
    std::fprintf(stderr, "nightlock: %s\n", message);
    return kExitUsage;
}

int failVault(VaultError error) {
    std::fprintf(stderr, "nightlock: %s\n", nightlock::errorMessage(error));
    return error == VaultError::WrongPassword ? kExitPassword : kExitIo;
}

std::optional<nightlock::VaultFile> openVault(const GlobalOptions& global,
                                              int& exitCode) {
    nightlock::secure::String password;
    if (!readPassword("Master password: ", global.passwordStdin, password)) {
        exitCode = fail("could not read the password");
        return std::nullopt;
    }
    auto vault = VaultFile::open(global.vaultPath,
                                 nightlock::secure::view(password));
    if (!vault) {
        exitCode = failVault(vault.error());
        return std::nullopt;
    }
    return vault.take();
}

std::string formatDate(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void printTree(const nightlock::Group& group, int depth) {
    std::printf("%*s%s/\n", depth * 2, "", group.name().c_str());
    for (const auto& child : group.groups())
        printTree(*child, depth + 1);
    for (const auto& entry : group.entries())
        std::printf("%*s%s\n", (depth + 1) * 2, "", entry->name.c_str());
}

int runInit(const GlobalOptions& global) {
    std::error_code ec;
    if (std::filesystem::exists(global.vaultPath, ec))
        return fail("vault already exists at that path");

    nightlock::secure::String password, confirm;
    if (!readPassword("New master password: ", global.passwordStdin, password))
        return fail("could not read the password");
    if (password.empty())
        return fail("the master password must not be empty");
    if (!global.passwordStdin) {
        if (!readPassword("Repeat password: ", false, confirm))
            return fail("could not read the password");
        if (password != confirm)
            return fail("passwords do not match");
    }

    auto vault = VaultFile::create(global.vaultPath,
                                   nightlock::secure::view(password));
    if (!vault)
        return failVault(vault.error());
    std::printf("Created %s\n", global.vaultPath.c_str());
    return kExitOk;
}

int runLs(const GlobalOptions& global, const std::vector<std::string>& args) {
    if (args.size() > 1)
        return fail("usage: nightlock ls [group]");
    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);
    if (!vault)
        return exitCode;

    const nightlock::Group* start =
        findGroup(*vault->root(), args.empty() ? "" : args[0]);
    if (!start)
        return fail("no such folder");
    printTree(*start, 0);
    return kExitOk;
}

int runShow(const GlobalOptions& global, const std::vector<std::string>& args) {
    bool reveal = false;
    std::string path;
    for (const std::string& arg : args) {
        if (arg == "-p" || arg == "--password")
            reveal = true;
        else if (path.empty())
            path = arg;
        else
            return fail("usage: nightlock show <entry> [-p]");
    }
    if (path.empty())
        return fail("usage: nightlock show <entry> [-p]");

    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);
    if (!vault)
        return exitCode;

    const EntryRef ref = findEntry(*vault->root(), path);
    if (!ref.entry)
        return fail("no such entry");
    const nightlock::Entry& e = *ref.entry;

    std::printf("%s\n", path.c_str());
    if (!e.login.empty())
        std::printf("  login:    %s\n", e.login.c_str());
    if (reveal)
        std::printf("  password: %.*s\n", static_cast<int>(e.password.size()),
                    e.password.data());
    else
        std::printf("  password: ********  (-p reveals)\n");
    if (!e.url.empty())
        std::printf("  url:      %s\n", e.url.c_str());
    if (!e.note.empty())
        std::printf("  note:     %s\n", e.note.c_str());
    if (!e.code.empty()) {
        if (reveal)
            std::printf("  code:     %.*s\n", static_cast<int>(e.code.size()),
                        e.code.data());
        else
            std::printf("  code:     ******\n");
    }
    std::printf("  created:  %s\n", formatDate(e.created).c_str());
    std::printf("  modified: %s\n", formatDate(e.modified).c_str());
    return kExitOk;
}

int runAdd(const GlobalOptions& global, const std::vector<std::string>& args) {
    std::string path, login, url, note;
    bool generate = false;
    nightlock::GeneratorOptions genOptions;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto valueOf = [&](const char* flag) -> const std::string* {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "nightlock: %s needs a value\n", flag);
                return nullptr;
            }
            return &args[++i];
        };
        if (arg == "--login") {
            const auto* v = valueOf("--login");
            if (!v) return kExitUsage;
            login = *v;
        } else if (arg == "--url") {
            const auto* v = valueOf("--url");
            if (!v) return kExitUsage;
            url = *v;
        } else if (arg == "--note") {
            const auto* v = valueOf("--note");
            if (!v) return kExitUsage;
            note = *v;
        } else if (arg == "--gen") {
            generate = true;
        } else if (arg == "--length") {
            const auto* v = valueOf("--length");
            if (!v) return kExitUsage;
            genOptions.length = std::atoi(v->c_str());
        } else if (arg == "--symbols") {
            genOptions.symbols = true;
        } else if (path.empty()) {
            path = arg;
        } else {
            return fail("usage: nightlock add <folder/name> [--login L] [--url U] "
                        "[--note N] [--gen [--length N] [--symbols]]");
        }
    }
    if (path.empty())
        return fail("usage: nightlock add <folder/name> [options]");

    const std::vector<std::string_view> parts = splitPath(path);
    if (parts.empty())
        return fail("the entry needs a name");

    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);
    if (!vault)
        return exitCode;

    nightlock::Entry entry;
    entry.name = std::string(parts.back());
    entry.login = login;
    entry.url = url;
    entry.note = note;
    entry.created = std::chrono::system_clock::now();
    entry.modified = entry.created;

    if (generate) {
        entry.password = nightlock::generatePassword(genOptions);
        if (entry.password.empty())
            return fail("generator options leave nothing to draw from");
    } else {
        if (!readPassword("Entry password: ", global.passwordStdin,
                          entry.password))
            return fail("could not read the entry password");
    }

    std::string parentPath;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (i)
            parentPath += '/';
        parentPath += parts[i];
    }
    nightlock::Group& parent = ensureGroup(*vault->root(), parentPath);
    const bool printGenerated = generate;
    const nightlock::Entry& stored = parent.addEntry(std::move(entry));

    if (const VaultError error = vault->save(); error != VaultError::None)
        return failVault(error);
    if (printGenerated)
        std::printf("Generated password: %.*s\n",
                    static_cast<int>(stored.password.size()),
                    stored.password.data());
    return kExitOk;
}

int runRm(const GlobalOptions& global, const std::vector<std::string>& args) {
    if (args.size() != 1)
        return fail("usage: nightlock rm <entry-or-empty-folder>");
    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);
    if (!vault)
        return exitCode;

    if (const EntryRef ref = findEntry(*vault->root(), args[0]); ref.entry) {
        ref.group->removeEntry(ref.entry);
    } else if (nightlock::Group* group = findGroup(*vault->root(), args[0])) {
        if (group == vault->root())
            return fail("cannot remove the root");
        if (!group->groups().empty() || !group->entries().empty())
            return fail("folder is not empty");
        group->parent()->removeGroup(group);
    } else {
        return fail("no such entry or folder");
    }

    if (const VaultError error = vault->save(); error != VaultError::None)
        return failVault(error);
    return kExitOk;
}

int runMkdir(const GlobalOptions& global, const std::vector<std::string>& args) {
    if (args.size() != 1 || splitPath(args[0]).empty())
        return fail("usage: nightlock mkdir <folder>");
    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);
    if (!vault)
        return exitCode;
    ensureGroup(*vault->root(), args[0]);
    if (const VaultError error = vault->save(); error != VaultError::None)
        return failVault(error);
    return kExitOk;
}

int runPasswd(const GlobalOptions& global) {
    int exitCode = kExitOk;
    auto vault = openVault(global, exitCode);  // prompts the old password
    if (!vault)
        return exitCode;

    nightlock::secure::String next, confirm;
    if (!readPassword("New master password: ", global.passwordStdin, next))
        return fail("could not read the password");
    if (next.empty())
        return fail("the master password must not be empty");
    if (!global.passwordStdin) {
        if (!readPassword("Repeat password: ", false, confirm))
            return fail("could not read the password");
        if (next != confirm)
            return fail("passwords do not match");
    }
    if (const VaultError error =
            vault->changePassword(nightlock::secure::view(next));
        error != VaultError::None)
        return failVault(error);
    std::printf("Master password changed\n");
    return kExitOk;
}

int runGen(const std::vector<std::string>& args) {
    nightlock::GeneratorOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--length" && i + 1 < args.size())
            options.length = std::atoi(args[++i].c_str());
        else if (arg == "--symbols")
            options.symbols = true;
        else if (arg == "--no-lower")
            options.lower = false;
        else if (arg == "--no-upper")
            options.upper = false;
        else if (arg == "--no-digits")
            options.digits = false;
        else if (arg == "--exclude" && i + 1 < args.size())
            options.exclude = args[++i];
        else
            return fail("usage: nightlock gen [--length N] [--symbols] "
                        "[--no-lower] [--no-upper] [--no-digits] [--exclude S]");
    }
    const nightlock::secure::String password =
        nightlock::generatePassword(options);
    if (password.empty())
        return fail("generator options leave nothing to draw from");
    std::printf("%.*s\n", static_cast<int>(password.size()), password.data());
    return kExitOk;
}

}  // namespace

std::filesystem::path defaultVaultPath() {
    if (const char* env = std::getenv("NIGHTLOCK_VAULT"); env && *env)
        return env;
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = home ? home : ".";
#ifdef __APPLE__
    return base / "Library/Application Support/Nightlock/Primary.nlck";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "nightlock/Primary.nlck";
    return base / ".local/share/nightlock/Primary.nlck";
#endif
}

void printUsage() {
    std::printf(
        "nightlock — encrypted password vault\n"
        "\n"
        "usage: nightlock [-f <vault.nlck>] [--password-stdin] <command> [args]\n"
        "\n"
        "commands:\n"
        "  init                      create a new vault\n"
        "  ls [group]                list folders and entries\n"
        "  show <entry> [-p]         print an entry (-p reveals secrets)\n"
        "  add <folder/name> [...]   add an entry; --login L --url U --note N,\n"
        "                            --gen [--length N] [--symbols] generates\n"
        "  rm <path>                 remove an entry or an empty folder\n"
        "  mkdir <folder>            create folders, missing parents included\n"
        "  passwd                    change the master password\n"
        "  gen [...]                 generate a password (no vault needed)\n"
        "\n"
        "options:\n"
        "  -f, --file <path>   vault file; else $NIGHTLOCK_VAULT, else\n"
        "                      %s\n"
        "  --password-stdin    read passphrases as stdin lines (scripts)\n"
        "  -h, --help          this help\n"
        "  -V, --version       version\n",
        defaultVaultPath().c_str());
}

int runCommand(const std::string& command, const GlobalOptions& global,
               const std::vector<std::string>& args) {
    if (command == "init")
        return runInit(global);
    if (command == "ls")
        return runLs(global, args);
    if (command == "show")
        return runShow(global, args);
    if (command == "add")
        return runAdd(global, args);
    if (command == "rm")
        return runRm(global, args);
    if (command == "mkdir")
        return runMkdir(global, args);
    if (command == "passwd")
        return runPasswd(global);
    if (command == "gen")
        return runGen(args);
    std::fprintf(stderr, "nightlock: unknown command '%s' (try --help)\n",
                 command.c_str());
    return kExitUsage;
}

}  // namespace cli
