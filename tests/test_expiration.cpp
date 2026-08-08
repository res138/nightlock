#include <doctest/doctest.h>

#include <nightlock/expiration.hpp>

using namespace nightlock;

namespace {

std::chrono::year_month_day date(int year, unsigned month, unsigned day) {
    return std::chrono::year(year) / month / day;
}

Entry expiringEntry(const char* label, const char* value) {
    Entry entry;
    EntryField expiration;
    expiration.label = label;
    expiration.value = value;
    expiration.custom = true;
    entry.fields.push_back(std::move(expiration));
    return entry;
}

}  // namespace

TEST_CASE("expiration recognizes only the reserved labels") {
    CHECK(expiration::isLabel("expiration"));
    CHECK(expiration::isLabel("Expiration"));
    CHECK(expiration::isLabel("EXPIRATION"));
    CHECK_FALSE(expiration::isLabel("ExPiRaTiOn"));
    CHECK_FALSE(expiration::isLabel("expiration date"));
}

TEST_CASE("expiration parses a strict valid calendar date") {
    CHECK(expiration::parseDate("12/12/2026") == date(2026, 12, 12));
    CHECK(expiration::parseDate("29/02/2028") == date(2028, 2, 29));
    CHECK_FALSE(expiration::parseDate("29/02/2027"));
    CHECK_FALSE(expiration::parseDate("1/12/2026"));
    CHECK_FALSE(expiration::parseDate("12.12.2026"));
}

TEST_CASE("entry expires on its expiration date") {
    for (EntryPreset preset : {EntryPreset::Classic, EntryPreset::Wifi,
                               EntryPreset::BankCard, EntryPreset::BrowserBookmark,
                               EntryPreset::CryptoWallet}) {
        Entry entry = expiringEntry("Expiration", "12/12/2026");
        entry.preset = preset;
        CHECK_FALSE(expiration::isExpired(entry, date(2026, 12, 11)));
        CHECK(expiration::isExpired(entry, date(2026, 12, 12)));
        CHECK(expiration::isExpired(entry, date(2027, 1, 1)));
    }
}

TEST_CASE("non-custom and malformed expiration fields do not expire an entry") {
    Entry entry = expiringEntry("expiration", "31/02/2026");
    CHECK_FALSE(expiration::isExpired(entry, date(2026, 3, 1)));
    entry.fields.front().value = "01/01/2020";
    entry.fields.front().custom = false;
    CHECK_FALSE(expiration::isExpired(entry, date(2026, 3, 1)));
}
