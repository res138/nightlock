#include <doctest/doctest.h>

#include <nightlock/crypto.hpp>

#include <array>
#include <cstdio>
#include <string>

using namespace nightlock;

namespace {

// Weak-but-valid parameters so the suite stays fast; production
// defaults are exercised through the same code path.
crypto::KdfParams testParams() {
    crypto::KdfParams p;
    p.memoryKiB = 8 * 1024;
    p.iterations = 1;
    return p;
}

std::array<std::uint8_t, crypto::kSaltBytes> fixedSalt() {
    std::array<std::uint8_t, crypto::kSaltBytes> salt{};
    for (std::size_t i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<std::uint8_t>(i + 1);
    return salt;
}

std::string toHex(const secure::Bytes& b) {
    std::string out;
    char buf[3];
    for (std::uint8_t byte : b) {
        std::snprintf(buf, sizeof(buf), "%02x", byte);
        out += buf;
    }
    return out;
}

}  // namespace

TEST_CASE("deriveKey is deterministic and matches the frozen vector") {
    const auto salt = fixedSalt();
    secure::Bytes key;
    REQUIRE(crypto::deriveKey(key, "correct horse battery staple", salt, testParams()));
    REQUIRE(key.size() == crypto::kKeyBytes);

    // Frozen once at introduction: silent drift of the KDF algorithm,
    // parameter interpretation or salt handling breaks this.
    CHECK(toHex(key) ==
          "63edd503cf1020024f59051aa0240d2b4f4ecd263a7286c8400a95a5e5be3209");

    secure::Bytes again;
    REQUIRE(crypto::deriveKey(again, "correct horse battery staple", salt, testParams()));
    CHECK(key == again);

    secure::Bytes other;
    REQUIRE(crypto::deriveKey(other, "correct horse battery staple?", salt, testParams()));
    CHECK(key != other);
}

TEST_CASE("deriveKey rejects unsupported parallelism") {
    crypto::KdfParams p = testParams();
    p.parallelism = 2;
    secure::Bytes key;
    CHECK_FALSE(crypto::deriveKey(key, "pw", fixedSalt(), p));
}

TEST_CASE("aead seal/open round-trip with associated data") {
    secure::Bytes key;
    REQUIRE(crypto::deriveKey(key, "pw", fixedSalt(), testParams()));

    std::array<std::uint8_t, crypto::kNonceBytes> nonce{};
    crypto::randomBytes(nonce.data(), nonce.size());

    const std::string payload = "tlv payload bytes";
    secure::Bytes plaintext(payload.begin(), payload.end());
    const std::vector<std::uint8_t> ad = {'h', 'd', 'r'};

    std::vector<std::uint8_t> sealed;
    REQUIRE(crypto::aeadSeal(sealed, plaintext, nonce, ad, key));
    CHECK(sealed.size() == plaintext.size() + crypto::kTagBytes);

    secure::Bytes opened;
    REQUIRE(crypto::aeadOpen(opened, sealed, nonce, ad, key));
    CHECK(opened == plaintext);

    SUBCASE("flipped ciphertext byte fails") {
        auto bad = sealed;
        bad[bad.size() / 2] ^= 0x01;
        CHECK_FALSE(crypto::aeadOpen(opened, bad, nonce, ad, key));
    }
    SUBCASE("flipped tag byte fails") {
        auto bad = sealed;
        bad.back() ^= 0x80;
        CHECK_FALSE(crypto::aeadOpen(opened, bad, nonce, ad, key));
    }
    SUBCASE("flipped associated data fails") {
        auto badAd = ad;
        badAd[0] ^= 0x01;
        CHECK_FALSE(crypto::aeadOpen(opened, sealed, nonce, badAd, key));
    }
    SUBCASE("flipped nonce fails") {
        auto badNonce = nonce;
        badNonce[0] ^= 0x01;
        CHECK_FALSE(crypto::aeadOpen(opened, sealed, badNonce, ad, key));
    }
    SUBCASE("wrong key fails") {
        secure::Bytes otherKey;
        REQUIRE(crypto::deriveKey(otherKey, "pw2", fixedSalt(), testParams()));
        CHECK_FALSE(crypto::aeadOpen(opened, sealed, nonce, ad, otherKey));
    }
    SUBCASE("truncated below the tag fails") {
        std::vector<std::uint8_t> stub(sealed.begin(), sealed.begin() + 8);
        CHECK_FALSE(crypto::aeadOpen(opened, stub, nonce, ad, key));
    }
}

TEST_CASE("aead handles empty plaintext") {
    secure::Bytes key;
    REQUIRE(crypto::deriveKey(key, "pw", fixedSalt(), testParams()));
    std::array<std::uint8_t, crypto::kNonceBytes> nonce{};
    secure::Bytes empty;
    std::vector<std::uint8_t> sealed;
    REQUIRE(crypto::aeadSeal(sealed, empty, nonce, {}, key));
    CHECK(sealed.size() == crypto::kTagBytes);
    secure::Bytes opened;
    REQUIRE(crypto::aeadOpen(opened, sealed, nonce, {}, key));
    CHECK(opened.empty());
}

TEST_CASE("randomUniform stays in range") {
    CHECK(crypto::randomUniform(0) == 0);
    CHECK(crypto::randomUniform(1) == 0);
    for (int i = 0; i < 1000; ++i)
        CHECK(crypto::randomUniform(7) < 7);
}
