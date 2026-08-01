#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "secure.hpp"

// Crypto facade for the vault format. Exactly one backend TU
// implements these functions (currently src/crypto/sodium_backend.cpp);
// swapping backends means swapping that one source file in CMake. The
// file format stays backend-neutral — the header stores cipher and KDF
// ids, not library names.

namespace nightlock::crypto {

inline constexpr std::size_t kKeyBytes = 32;
inline constexpr std::size_t kSaltBytes = 16;
inline constexpr std::size_t kNonceBytes = 24;
inline constexpr std::size_t kTagBytes = 16;

// File-header identifiers.
inline constexpr std::uint32_t kKdfArgon2id = 1;
inline constexpr std::uint32_t kCipherXChaCha20Poly1305 = 1;

// Stored verbatim in the vault file header.
struct KdfParams {
    std::uint32_t memoryKiB = 64 * 1024;  // 64 MiB
    std::uint32_t iterations = 3;
    std::uint32_t parallelism = 1;  // the sodium backend supports only 1
};

// CSPRNG.
void randomBytes(void* out, std::size_t len);
// Unbiased draw from [0, upperBound); upperBound 0 returns 0.
std::uint32_t randomUniform(std::uint32_t upperBound);

// Argon2id password -> 32-byte key in pinned memory. Fails on
// unsupported params (parallelism != 1) or KDF out-of-memory.
bool deriveKey(secure::Bytes& keyOut, std::string_view password,
               std::span<const std::uint8_t, kSaltBytes> salt,
               const KdfParams& params);

// Constant-time equality for secrets (key comparisons). The length
// check is ordinary — sizes here are format constants, not secrets.
bool secretEquals(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);

// out = ciphertext || 16-byte tag. `ad` is authenticated, not encrypted.
bool aeadSeal(std::vector<std::uint8_t>& out, const secure::Bytes& plaintext,
              std::span<const std::uint8_t, kNonceBytes> nonce,
              std::span<const std::uint8_t> ad, const secure::Bytes& key);

// False on authentication failure — wrong key or tampered
// ciphertext/ad/nonce are indistinguishable by design.
bool aeadOpen(secure::Bytes& out, std::span<const std::uint8_t> ciphertextAndTag,
              std::span<const std::uint8_t, kNonceBytes> nonce,
              std::span<const std::uint8_t> ad, const secure::Bytes& key);

}  // namespace nightlock::crypto
