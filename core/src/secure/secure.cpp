#include "nightlock/secure.hpp"

#include <sodium.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "secure/init.hpp"

namespace nightlock::secure {

namespace detail {

void ensureSodium() noexcept {
    static std::once_flag flag;
    std::call_once(flag, [] {
        if (sodium_init() < 0) {
            std::fputs("nightlock: libsodium initialization failed\n", stderr);
            std::abort();
        }
    });
}

}  // namespace detail

void zeroize(void* p, std::size_t n) noexcept {
    if (p && n)
        sodium_memzero(p, n);
}

bool lockPages(void* p, std::size_t n) noexcept {
    if (!p || !n)
        return true;
    detail::ensureSodium();
    return sodium_mlock(p, n) == 0;
}

void unlockPages(void* p, std::size_t n) noexcept {
    if (!p || !n)
        return;
    detail::ensureSodium();
    // sodium_munlock zeroizes the range before unpinning, and the
    // zeroize happens even when the mlock never succeeded.
    sodium_munlock(p, n);
}

void wipe(String& s) noexcept {
    // Padding to capacity() never reallocates, so this scrubs the tail
    // bytes of earlier, longer values through defined behavior.
    s.resize(s.capacity(), '\0');
    if (!s.empty())
        zeroize(s.data(), s.size());
    s.clear();
    s.shrink_to_fit();
}

}  // namespace nightlock::secure
