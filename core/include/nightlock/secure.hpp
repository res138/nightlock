#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace nightlock::secure {

// Overwrites n bytes in a way the optimizer cannot elide.
void zeroize(void* p, std::size_t n) noexcept;

// Pins pages so the secret stays out of swap. Best-effort: fails
// under RLIMIT_MEMLOCK or on exotic platforms, and callers ignore
// that — the zeroize guarantees below hold either way.
bool lockPages(void* p, std::size_t n) noexcept;

// Zeroizes the range first, then unpins it (works even when the
// earlier lockPages failed).
void unlockPages(void* p, std::size_t n) noexcept;

// Allocator for secret-holding containers: pins pages on allocate,
// zeroizes and unpins on deallocate.
//
// Caveat: std::basic_string keeps short payloads inline (SSO), and
// those bytes never pass through the allocator. wipe() below covers a
// live string including the inline case; VaultFile::lock() wipes every
// secret in the tree before destroying it. Forcing heap storage by
// over-reserving on each assignment was rejected — it complicates
// every call site to close only a fraction of the remaining gap
// (moves and copies still leave transient inline bytes around).
template <class T>
struct Allocator {
    using value_type = T;

    Allocator() noexcept = default;
    template <class U>
    Allocator(const Allocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n > static_cast<std::size_t>(-1) / sizeof(T))
            throw std::bad_alloc();
        T* p = static_cast<T*>(::operator new(n * sizeof(T)));
        lockPages(p, n * sizeof(T));
        return p;
    }

    void deallocate(T* p, std::size_t n) noexcept {
        unlockPages(p, n * sizeof(T));
        ::operator delete(p);
    }

    template <class U>
    bool operator==(const Allocator<U>&) const noexcept {
        return true;
    }
};

using Bytes = std::vector<std::uint8_t, Allocator<std::uint8_t>>;
using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

// Overwrites the whole live buffer — heap or SSO-inline, including the
// capacity tail left over from earlier longer values — then clears and
// releases any heap storage.
void wipe(String& s) noexcept;

inline std::string_view view(const String& s) noexcept {
    return {s.data(), s.size()};
}

}  // namespace nightlock::secure
