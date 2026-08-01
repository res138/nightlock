#include "termio.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <csignal>
#include <cstdio>

namespace cli {

namespace {

// The guard state is reachable from the SIGINT handler, so a Ctrl-C
// during the prompt cannot leave the terminal with echo off.
#ifdef _WIN32

DWORD g_savedMode = 0;
HANDLE g_console = nullptr;
volatile sig_atomic_t g_echoOff = 0;

void restoreEcho() {
    if (g_echoOff) {
        SetConsoleMode(g_console, g_savedMode);
        g_echoOff = 0;
    }
}

void onInterrupt(int) {
    restoreEcho();
    _exit(130);
}

struct EchoGuard {
    void (*previousHandler)(int) = SIG_DFL;
    bool active = false;

    bool disable() {
        if (!_isatty(_fileno(stdin)))
            return true;  // piped input: nothing to hide
        g_console = GetStdHandle(STD_INPUT_HANDLE);
        if (g_console == INVALID_HANDLE_VALUE ||
            !GetConsoleMode(g_console, &g_savedMode))
            return false;
        if (!SetConsoleMode(g_console, g_savedMode & ~ENABLE_ECHO_INPUT))
            return false;
        g_echoOff = 1;
        previousHandler = std::signal(SIGINT, onInterrupt);
        active = true;
        return true;
    }

    ~EchoGuard() {
        if (active) {
            restoreEcho();
            std::signal(SIGINT, previousHandler);
        }
    }
};

#else

termios g_saved;
volatile sig_atomic_t g_echoOff = 0;

void restoreEcho() {
    if (g_echoOff) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
        g_echoOff = 0;
    }
}

void onInterrupt(int) {
    restoreEcho();
    _exit(130);
}

struct EchoGuard {
    void (*previousHandler)(int) = SIG_DFL;
    bool active = false;

    bool disable() {
        if (!isatty(STDIN_FILENO))
            return true;  // piped input: nothing to hide
        if (tcgetattr(STDIN_FILENO, &g_saved) != 0)
            return false;
        termios muted = g_saved;
        muted.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &muted) != 0)
            return false;
        g_echoOff = 1;
        previousHandler = std::signal(SIGINT, onInterrupt);
        active = true;
        return true;
    }

    ~EchoGuard() {
        if (active) {
            restoreEcho();
            std::signal(SIGINT, previousHandler);
        }
    }
};

#endif

bool readLine(nightlock::secure::String& out) {
    out.clear();
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n')
        out.push_back(static_cast<char>(c));
    // Binary-piped CRLF input on Windows leaves the '\r' behind.
    if (!out.empty() && out.back() == '\r')
        out.pop_back();
    return c == '\n' || !out.empty();
}

}  // namespace

bool readPassword(const char* prompt, bool stdinMode,
                  nightlock::secure::String& out) {
    if (stdinMode)
        return readLine(out);

    std::fputs(prompt, stderr);
    std::fflush(stderr);
    EchoGuard guard;
    if (!guard.disable())
        return false;
    const bool ok = readLine(out);
    std::fputc('\n', stderr);
    return ok;
}

}  // namespace cli
