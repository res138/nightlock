#pragma once

#include <nightlock/secure.hpp>

namespace cli {

// Reads a passphrase into pinned memory. Interactive mode prompts on
// stderr with terminal echo off (restored on every exit path,
// including Ctrl-C). stdinMode reads one plain line instead — the
// --password-stdin contract for scripts and tests.
bool readPassword(const char* prompt, bool stdinMode,
                  nightlock::secure::String& out);

}  // namespace cli
