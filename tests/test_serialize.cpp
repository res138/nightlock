#include <doctest/doctest.h>

#include <format/serialize.hpp>
#include <format/tlv.hpp>

#include <chrono>

using namespace nightlock;
using namespace nightlock::format;

namespace {

std::chrono::system_clock::time_point date(int y, int m, int d) {
    using namespace std::chrono;
    return sys_days(year(y) / m / d);
}

Entry makeEntry(std::string name) {
    Entry e;
    e.name = std::move(name);
    e.login = "user@" + e.name;
    e.password = "pw-secret";
    e.created = date(2020, 3, 27);
    e.modified = date(2021, 5, 18);
    return e;
}

// Deep equality, including child order.
void checkEqual(const Group& a, const Group& b) {
    CHECK(a.name() == b.name());
    CHECK(a.icon() == b.icon());
    REQUIRE(a.entries().size() == b.entries().size());
    for (std::size_t i = 0; i < a.entries().size(); ++i) {
        const Entry& x = *a.entries()[i];
        const Entry& y = *b.entries()[i];
        CHECK(x.name == y.name);
        CHECK(x.login == y.login);
        CHECK(x.password == y.password);
        // Avoid decomposing chrono values: libc++'s diagnostic formatter for
        // them raises the test binary's minimum OS requirement to macOS 13.3.
        CHECK(bool(x.created == y.created));
        CHECK(bool(x.modified == y.modified));
        CHECK(x.url == y.url);
        CHECK(x.icon == y.icon);
        CHECK(x.note == y.note);
        CHECK(x.code == y.code);
        CHECK(x.pattern == y.pattern);
        CHECK(x.preset == y.preset);
        CHECK(x.color == y.color);
        CHECK(x.fields == y.fields);
    }
    REQUIRE(a.groups().size() == b.groups().size());
    for (std::size_t i = 0; i < a.groups().size(); ++i)
        checkEqual(*a.groups()[i], *b.groups()[i]);
}

}  // namespace

TEST_CASE("vault round-trips through the payload") {
    Group root("Root");
    root.setIcon("vault-icon");
    Group& work = root.addGroup("Work");
    work.setIcon("briefcase");
    Group& banking = work.addGroup("Banking");
    root.addGroup("Empty");

    Entry full = makeEntry("GitHub");
    full.url = "https://github.com/";
    full.icon = "gh.svg";
    full.note = "primary account";
    full.code = "803 059";
    full.pattern = Pattern::Halo;
    full.preset = EntryPreset::BankCard;
    full.color = EntryColor::Purple;
    EntryField cardholder;
    cardholder.label = "Cardholder Name";
    cardholder.value = "Octo Cat";
    full.fields.push_back(std::move(cardholder));
    EntryField cvv;
    cvv.label = "CVV";
    cvv.value = "123";
    cvv.secret = true;
    cvv.custom = true;
    full.fields.push_back(std::move(cvv));
    banking.addEntry(std::move(full));

    banking.addEntry(makeEntry("Zeta"));   // order matters:
    banking.addEntry(makeEntry("Alpha"));  // Zeta stays before Alpha
    work.addEntry(makeEntry("Meta"));

    Entry sparse;  // everything optional absent, dates default
    banking.addEntry(std::move(sparse));

    secure::Bytes payload;
    serializeVault(root, payload);

    std::unique_ptr<Group> rebuilt;
    REQUIRE(deserializeVault(payload, rebuilt) == ParseResult::Ok);
    REQUIRE(rebuilt);
    checkEqual(root, *rebuilt);
    CHECK(rebuilt->groups()[0]->parent() == rebuilt.get());
}

TEST_CASE("every pattern value survives the trip") {
    Group root("Root");
    for (std::uint32_t i = 0; i <= static_cast<std::uint32_t>(Pattern::Halo); ++i) {
        Entry e = makeEntry("p" + std::to_string(i));
        e.pattern = static_cast<Pattern>(i);
        root.addEntry(std::move(e));
    }
    secure::Bytes payload;
    serializeVault(root, payload);
    std::unique_ptr<Group> rebuilt;
    REQUIRE(deserializeVault(payload, rebuilt) == ParseResult::Ok);
    checkEqual(root, *rebuilt);
}

TEST_CASE("unknown non-critical tags are skipped") {
    // A hand-built payload sprinkled with tags from an imaginary
    // future revision, none of them critical.
    secure::Bytes payload;
    TlvWriter w(payload);
    const auto meta = w.beginContainer(kTagMeta);
    w.u32(kTagPayloadVersion, kPayloadVersion);
    w.string(0x0177, "future meta field");
    w.endContainer(meta);
    w.string(0x0570, "future top-level record");
    const auto g = w.beginContainer(kTagGroup);
    w.string(kTagGroupName, "Root");
    w.string(0x0270, "future group field");
    const auto e = w.beginContainer(kTagEntry);
    w.string(kTagEntryName, "Kept");
    w.string(0x0370, "future entry field");
    w.i64(kTagEntryCreatedMs, 0);
    w.i64(kTagEntryModifiedMs, 0);
    w.endContainer(e);
    w.endContainer(g);

    std::unique_ptr<Group> rebuilt;
    REQUIRE(deserializeVault(payload, rebuilt) == ParseResult::Ok);
    CHECK(rebuilt->name() == "Root");
    REQUIRE(rebuilt->entries().size() == 1);
    CHECK(rebuilt->entries()[0]->name == "Kept");
}

TEST_CASE("unknown critical tag is Unsupported") {
    secure::Bytes payload;
    TlvWriter w(payload);
    const auto meta = w.beginContainer(kTagMeta);
    w.u32(kTagPayloadVersion, kPayloadVersion);
    w.endContainer(meta);
    const auto g = w.beginContainer(kTagGroup);
    w.string(kTagGroupName, "Root");
    w.string(0x8270, "mandatory future group feature");
    w.endContainer(g);

    std::unique_ptr<Group> rebuilt;
    CHECK(deserializeVault(payload, rebuilt) == ParseResult::Unsupported);
}

TEST_CASE("newer payload version is Unsupported") {
    secure::Bytes payload;
    TlvWriter w(payload);
    const auto meta = w.beginContainer(kTagMeta);
    w.u32(kTagPayloadVersion, kPayloadVersion + 1);
    w.endContainer(meta);
    const auto g = w.beginContainer(kTagGroup);
    w.string(kTagGroupName, "Root");
    w.endContainer(g);

    std::unique_ptr<Group> rebuilt;
    CHECK(deserializeVault(payload, rebuilt) == ParseResult::Unsupported);
}

TEST_CASE("malformed payloads are rejected") {
    Group root("Root");
    secure::Bytes payload;
    serializeVault(root, payload);

    SUBCASE("truncation") {
        payload.resize(payload.size() - 3);
        std::unique_ptr<Group> rebuilt;
        CHECK(deserializeVault(payload, rebuilt) == ParseResult::Malformed);
        CHECK(!rebuilt);
    }
    SUBCASE("missing meta") {
        secure::Bytes bare;
        TlvWriter w(bare);
        const auto g = w.beginContainer(kTagGroup);
        w.string(kTagGroupName, "Root");
        w.endContainer(g);
        std::unique_ptr<Group> rebuilt;
        CHECK(deserializeVault(bare, rebuilt) == ParseResult::Malformed);
    }
    SUBCASE("missing root group") {
        secure::Bytes bare;
        TlvWriter w(bare);
        const auto meta = w.beginContainer(kTagMeta);
        w.u32(kTagPayloadVersion, kPayloadVersion);
        w.endContainer(meta);
        std::unique_ptr<Group> rebuilt;
        CHECK(deserializeVault(bare, rebuilt) == ParseResult::Malformed);
    }
    SUBCASE("two root groups") {
        secure::Bytes bare;
        TlvWriter w(bare);
        const auto meta = w.beginContainer(kTagMeta);
        w.u32(kTagPayloadVersion, kPayloadVersion);
        w.endContainer(meta);
        for (int i = 0; i < 2; ++i) {
            const auto g = w.beginContainer(kTagGroup);
            w.string(kTagGroupName, "Root");
            w.endContainer(g);
        }
        std::unique_ptr<Group> rebuilt;
        CHECK(deserializeVault(bare, rebuilt) == ParseResult::Malformed);
    }
    SUBCASE("bad timestamp size") {
        secure::Bytes bare;
        TlvWriter w(bare);
        const auto meta = w.beginContainer(kTagMeta);
        w.u32(kTagPayloadVersion, kPayloadVersion);
        w.endContainer(meta);
        const auto g = w.beginContainer(kTagGroup);
        const auto e = w.beginContainer(kTagEntry);
        w.u32(kTagEntryCreatedMs, 5);  // 4 bytes where 8 are required
        w.endContainer(e);
        w.endContainer(g);
        std::unique_ptr<Group> rebuilt;
        CHECK(deserializeVault(bare, rebuilt) == ParseResult::Malformed);
    }
}
