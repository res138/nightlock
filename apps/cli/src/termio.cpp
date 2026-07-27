#include "termio.hpp"

#include <termios.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>

namespace cli {

namespace {

// The guard is reachable from the SIGINT handler, so a Ctrl-C during
// the prompt cannot leave the terminal with echo off.
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

bool readLine(nightlock::secure::String& out) {
    out.clear();
    int c;
    while ((c = std::fgetc(stdin)) != EOF && c != '\n')
        out.push_back(static_cast<char>(c));
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
