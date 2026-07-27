#include <doctest/doctest.h>

#include <format/tlv.hpp>

using namespace nightlock;
using namespace nightlock::format;

TEST_CASE("scalar round-trips through put/get") {
    secure::Bytes buf;
    putU16(buf, 0xBEEF);
    putU32(buf, 0xDEADBEEF);
    putU64(buf, 0x0102030405060708ULL);
    CHECK(getU16(buf) == 0xBEEF);
    CHECK(getU32(std::span(buf).subspan(2)) == 0xDEADBEEF);
    CHECK(getU64(std::span(buf).subspan(6)) == 0x0102030405060708ULL);
    // Little-endian on the wire.
    CHECK(buf[0] == 0xEF);
    CHECK(buf[1] == 0xBE);
    CHECK(buf[2] == 0xEF);
}

TEST_CASE("writer/reader round-trip of a flat sequence") {
    secure::Bytes buf;
    TlvWriter w(buf);
    w.string(0x0101, "hello");
    w.u32(0x0102, 42);
    w.i64(0x0103, -5);
    w.string(0x0104, "");

    TlvReader r(buf);
    REQUIRE(r.next());
    CHECK(r.tag() == 0x0101);
    CHECK(r.valueString() == "hello");
    CHECK_FALSE(r.valueU32().has_value());  // wrong size
    REQUIRE(r.next());
    CHECK(r.valueU32() == 42);
    REQUIRE(r.next());
    CHECK(r.valueI64() == -5);
    REQUIRE(r.next());
    CHECK(r.value().empty());
    CHECK_FALSE(r.next());
    CHECK_FALSE(r.malformed());
}

TEST_CASE("containers nest and the length patch holds") {
    secure::Bytes buf;
    TlvWriter w(buf);
    const auto outer = w.beginContainer(0x0001);
    w.string(0x0011, "inner");
    const auto nested = w.beginContainer(0x0002);
    w.u32(0x0021, 7);
    w.endContainer(nested);
    w.endContainer(outer);

    TlvReader top(buf);
    REQUIRE(top.next());
    CHECK(top.tag() == 0x0001);
    TlvReader in = top.container();
    REQUIRE(in.next());
    CHECK(in.valueString() == "inner");
    REQUIRE(in.next());
    CHECK(in.tag() == 0x0002);
    TlvReader deepest = in.container();
    REQUIRE(deepest.next());
    CHECK(deepest.valueU32() == 7);
    CHECK_FALSE(top.next());  // single top-level record
}

TEST_CASE("truncated record head is malformed") {
    secure::Bytes buf;
    TlvWriter w(buf);
    w.u32(0x0102, 42);
    buf.resize(3);  // mid-head
    TlvReader r(buf);
    CHECK_FALSE(r.next());
    CHECK(r.malformed());
}

TEST_CASE("length overrunning the buffer is malformed") {
    secure::Bytes buf;
    putU16(buf, 0x0101);
    putU32(buf, 100);  // claims 100 value bytes...
    buf.push_back('x');  // ...delivers one
    TlvReader r(buf);
    CHECK_FALSE(r.next());
    CHECK(r.malformed());
}

TEST_CASE("empty input is a clean end, not malformed") {
    TlvReader r({});
    CHECK_FALSE(r.next());
    CHECK_FALSE(r.malformed());
}
