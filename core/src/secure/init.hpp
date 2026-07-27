#pragma once

namespace nightlock::secure::detail {

// sodium_init() exactly once, from whichever module touches libsodium
// first. Aborts on failure: a password manager must not limp along
// with uninitialized crypto.
void ensureSodium() noexcept;

}  // namespace nightlock::secure::detail
