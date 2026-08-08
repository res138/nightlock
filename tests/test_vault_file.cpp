#include <doctest/doctest.h>

#include <nightlock/vault_file.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace nightlock;
namespace fs = std::filesystem;

namespace {

// Weak-but-valid KDF params: the suite derives keys dozens of times.
crypto::KdfParams weakParams() {
    crypto::KdfParams p;
    p.memoryKiB = 8 * 1024;
    p.iterations = 1;
    return p;
}

// Fresh directory per test case, under the build dir.
fs::path freshDir(const char* name) {
    const fs::path dir = fs::path("vault-tests") / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::vector<std::uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const fs::path& p, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

void populate(Group& root) {
    Group& work = root.addGroup("Work");
    Entry e;
    e.name = "GitHub";
    e.login = "octocat";
    e.password = "gH-repo$2020x8";
    e.code = "803 059";
    work.addEntry(std::move(e));
}

}  // namespace

TEST_CASE("create then reopen round-trips the tree") {
    const fs::path file = freshDir("roundtrip") / "Primary.nlck";

    auto created = VaultFile::create(file, "master-pw", weakParams());
    REQUIRE(created);
    CHECK(created.value().root()->name() == "Primary");  // named after the stem
    populate(*created.value().root());
    REQUIRE(created.value().save() == VaultError::None);

    auto reopened = VaultFile::open(file, "master-pw");
    REQUIRE(reopened);
    Group* root = reopened.value().root();
    REQUIRE(root->groups().size() == 1);
    REQUIRE(root->groups()[0]->entries().size() == 1);
    const Entry& entry = *root->groups()[0]->entries()[0];
    CHECK(entry.name == "GitHub");
    CHECK(secure::view(entry.password) == "gH-repo$2020x8");
    CHECK(secure::view(entry.code) == "803 059");
    CHECK(reopened.value().kdfParams().memoryKiB == weakParams().memoryKiB);
    CHECK(reopened.value().kdfParams().iterations == weakParams().iterations);
}

TEST_CASE("wrong password fails as WrongPassword") {
    const fs::path file = freshDir("wrongpw") / "v.nlck";
    REQUIRE(VaultFile::create(file, "right", weakParams()));
    auto attempt = VaultFile::open(file, "wrong");
    CHECK_FALSE(attempt);
    CHECK(attempt.error() == VaultError::WrongPassword);
}

TEST_CASE("plaintext secrets never reach the disk image") {
    const fs::path file = freshDir("noplain") / "v.nlck";
    auto vault = VaultFile::create(file, "master-pw", weakParams());
    REQUIRE(vault);
    populate(*vault.value().root());
    REQUIRE(vault.value().save() == VaultError::None);

    const auto raw = readFile(file);
    const std::string needle = "gH-repo$2020x8";
    const auto it = std::search(raw.begin(), raw.end(), needle.begin(), needle.end());
    CHECK(it == raw.end());
    // Magic is there, though.
    REQUIRE(raw.size() >= 4);
    CHECK(raw[0] == 'N');
    CHECK(raw[3] == 'K');
}

TEST_CASE("byte flips are rejected at every layer") {
    const fs::path file = freshDir("tamper") / "v.nlck";
    REQUIRE(VaultFile::create(file, "pw", weakParams()));
    const auto original = readFile(file);

    SUBCASE("magic -> NotAVault") {
        auto bad = original;
        bad[1] ^= 0xFF;
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::NotAVault);
    }
    SUBCASE("format version -> UnsupportedVersion") {
        auto bad = original;
        bad[4] ^= 0x02;
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::UnsupportedVersion);
    }
    SUBCASE("KDF memory field -> authenticated away as WrongPassword") {
        auto bad = original;
        bad[16] ^= 0x01;  // still a sane value, but the AAD changed
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::WrongPassword);
    }
    SUBCASE("salt -> WrongPassword") {
        auto bad = original;
        bad[30] ^= 0x01;
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::WrongPassword);
    }
    SUBCASE("ciphertext middle -> WrongPassword") {
        auto bad = original;
        bad[76 + (bad.size() - 76) / 2] ^= 0x01;
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::WrongPassword);
    }
    SUBCASE("absurd KDF params -> InvalidHeader") {
        auto bad = original;
        // memory KiB u32 at offset 16 -> 5 GiB
        const std::uint32_t huge = 5u * 1024 * 1024;
        for (int i = 0; i < 4; ++i)
            bad[16 + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(huge >> 8 * i);
        writeFile(file, bad);
        CHECK(VaultFile::open(file, "pw").error() == VaultError::InvalidHeader);
    }
}

TEST_CASE("truncated files never crash the reader") {
    const fs::path file = freshDir("truncate") / "v.nlck";
    REQUIRE(VaultFile::create(file, "pw", weakParams()));
    const auto original = readFile(file);

    const std::size_t cuts[] = {0, 3, 10, 75, 76, 80,
                                original.size() / 2, original.size() - 1};
    for (const std::size_t cut : cuts) {
        CAPTURE(cut);
        writeFile(file, {original.begin(), original.begin() + static_cast<long>(cut)});
        auto attempt = VaultFile::open(file, "pw");
        CHECK_FALSE(attempt);
        CHECK((attempt.error() == VaultError::InvalidHeader ||
               attempt.error() == VaultError::NotAVault ||
               attempt.error() == VaultError::WrongPassword));
    }
}

TEST_CASE("save keeps the previous image as .bak") {
    const fs::path dir = freshDir("backup");
    const fs::path file = dir / "v.nlck";
    auto vault = VaultFile::create(file, "pw", weakParams());
    REQUIRE(vault);
    const auto firstImage = readFile(file);

    populate(*vault.value().root());
    REQUIRE(vault.value().save() == VaultError::None);

    const fs::path bak = dir / "v.nlck.bak";
    REQUIRE(fs::exists(bak));
    CHECK(readFile(bak) == firstImage);
    CHECK(readFile(file) != firstImage);
    CHECK_FALSE(fs::exists(dir / "v.nlck.tmp"));

    // Both generations open.
    CHECK(VaultFile::open(file, "pw"));
    CHECK(VaultFile::open(bak, "pw"));
}

TEST_CASE("saveAs writes a new vault and adopts its path") {
    const fs::path dir = freshDir("save-as");
    const fs::path original = dir / "original.nlck";
    const fs::path copy = dir / "copy.nlck";
    auto vault = VaultFile::create(original, "pw", weakParams());
    REQUIRE(vault);
    populate(*vault.value().root());

    REQUIRE(vault.value().saveAs(copy) == VaultError::None);
    CHECK(vault.value().path() == copy);
    CHECK(fs::exists(original));
    CHECK(fs::exists(copy));

    auto reopened = VaultFile::open(copy, "pw");
    REQUIRE(reopened);
    CHECK(reopened.value().root()->groups()[0]->entries()[0]->name == "GitHub");
}

TEST_CASE("changePassword rotates the salt and invalidates the old password") {
    const fs::path file = freshDir("passwd") / "v.nlck";
    auto vault = VaultFile::create(file, "old-pw", weakParams());
    REQUIRE(vault);
    populate(*vault.value().root());
    REQUIRE(vault.value().save() == VaultError::None);
    const auto before = readFile(file);

    REQUIRE(vault.value().changePassword("new-pw") == VaultError::None);
    const auto after = readFile(file);
    // Salt lives at [28, 44).
    CHECK_FALSE(std::equal(before.begin() + 28, before.begin() + 44,
                           after.begin() + 28));

    CHECK(VaultFile::open(file, "old-pw").error() == VaultError::WrongPassword);
    auto reopened = VaultFile::open(file, "new-pw");
    REQUIRE(reopened);
    CHECK(reopened.value().root()->groups()[0]->entries()[0]->name == "GitHub");
}

TEST_CASE("verifyPassword accepts the current password only") {
    const fs::path file = freshDir("verify") / "v.nlck";
    auto vault = VaultFile::create(file, "right-pw", weakParams());
    REQUIRE(vault);

    CHECK(vault.value().verifyPassword("right-pw"));
    CHECK_FALSE(vault.value().verifyPassword("wrong-pw"));
    CHECK_FALSE(vault.value().verifyPassword(""));

    // The check tracks a password change.
    REQUIRE(vault.value().changePassword("next-pw") == VaultError::None);
    CHECK_FALSE(vault.value().verifyPassword("right-pw"));
    CHECK(vault.value().verifyPassword("next-pw"));

    // A locked vault has no key to compare against.
    vault.value().lock();
    CHECK_FALSE(vault.value().verifyPassword("next-pw"));
}

TEST_CASE("lock drops the tree and further saves report NotOpen") {
    const fs::path file = freshDir("lock") / "v.nlck";
    auto vault = VaultFile::create(file, "pw", weakParams());
    REQUIRE(vault);
    vault.value().lock();
    CHECK_FALSE(vault.value().isOpen());
    CHECK(vault.value().root() == nullptr);
    CHECK(vault.value().save() == VaultError::NotOpen);
    CHECK(vault.value().changePassword("x") == VaultError::NotOpen);
}

TEST_CASE("open on a missing file is IoError") {
    CHECK(VaultFile::open("vault-tests/definitely-missing.nlck", "pw").error() ==
          VaultError::IoError);
}

TEST_CASE("create rejects insane params") {
    crypto::KdfParams bad = weakParams();
    bad.parallelism = 4;
    CHECK(VaultFile::create("vault-tests/x.nlck", "pw", bad).error() ==
          VaultError::InvalidHeader);
}
