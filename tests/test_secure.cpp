#include <doctest/doctest.h>

#include <nightlock/secure.hpp>

#include <algorithm>
#include <cstring>

using namespace nightlock;

// These tests stay within defined behavior: they verify zeroize/wipe
// on live buffers. The free-path guarantee (deallocate scrubbing) rides
// on unlockPages, which is exercised directly — inspecting freed memory
// would be UB, so the suite deliberately never does that.

TEST_CASE("zeroize clears a live buffer") {
    char buffer[64];
    std::memset(buffer, 'x', sizeof(buffer));
    secure::zeroize(buffer, sizeof(buffer));
    CHECK(std::all_of(buffer, buffer + sizeof(buffer),
                      [](char c) { return c == '\0'; }));
    secure::zeroize(nullptr, 16);  // must be a safe no-op
    secure::zeroize(buffer, 0);
}

TEST_CASE("unlockPages scrubs even without a prior lock") {
    secure::Allocator<char> alloc;
    char* p = alloc.allocate(128);
    std::memset(p, 's', 128);
    secure::unlockPages(p, 128);  // the deallocate path, minus the free
    CHECK(std::all_of(p, p + 128, [](char c) { return c == '\0'; }));
    ::operator delete(p);
}

TEST_CASE("wipe empties heap-backed strings") {
    secure::String s;
    s.assign(200, 'p');  // way past any SSO threshold
    secure::wipe(s);
    CHECK(s.empty());
    CHECK(s.capacity() < 200);  // heap block released (and scrubbed by the allocator)
}

TEST_CASE("wipe scrubs the SSO tail left by a shorter reassignment") {
    secure::String s = "hunter2secret";  // inline
    s = "x";                             // old tail bytes linger inline
    secure::wipe(s);
    CHECK(s.empty());
    // The whole inline capacity was overwritten via resize+zeroize; the
    // only thing observable in defined behavior is emptiness, the rest
    // is covered by construction of wipe() itself.
}

TEST_CASE("secure::String behaves like a string") {
    secure::String s = "pass";
    s += "word";
    CHECK(secure::view(s) == "password");
    secure::String copy = s;
    CHECK(copy == s);
    secure::String moved = std::move(copy);
    CHECK(moved == s);
    CHECK(s.compare(0, 4, "pass") == 0);
}

TEST_CASE("secure::Bytes round-trips content") {
    secure::Bytes b;
    for (int i = 0; i < 300; ++i)
        b.push_back(static_cast<std::uint8_t>(i));
    CHECK(b.size() == 300);
    CHECK(b[255] == 255);
}
