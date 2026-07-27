#include <sodium.h>

#include "nightlock/crypto.hpp"
#include "secure/init.hpp"

namespace nightlock::crypto {

static_assert(kKeyBytes == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(kSaltBytes == crypto_pwhash_argon2id_SALTBYTES);
static_assert(kNonceBytes == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
static_assert(kTagBytes == crypto_aead_xchacha20poly1305_ietf_ABYTES);

void randomBytes(void* out, std::size_t len) {
    secure::detail::ensureSodium();
    randombytes_buf(out, len);
}

std::uint32_t randomUniform(std::uint32_t upperBound) {
    if (upperBound == 0)
        return 0;
    secure::detail::ensureSodium();
    return randombytes_uniform(upperBound);
}

bool deriveKey(secure::Bytes& keyOut, std::string_view password,
               std::span<const std::uint8_t, kSaltBytes> salt,
               const KdfParams& params) {
    secure::detail::ensureSodium();
    if (params.parallelism != 1)
        return false;  // libsodium's Argon2id is single-lane
    keyOut.assign(kKeyBytes, 0);
    return crypto_pwhash(keyOut.data(), keyOut.size(), password.data(),
                         password.size(), salt.data(), params.iterations,
                         static_cast<std::size_t>(params.memoryKiB) * 1024,
                         crypto_pwhash_ALG_ARGON2ID13) == 0;
}

bool aeadSeal(std::vector<std::uint8_t>& out, const secure::Bytes& plaintext,
              std::span<const std::uint8_t, kNonceBytes> nonce,
              std::span<const std::uint8_t> ad, const secure::Bytes& key) {
    secure::detail::ensureSodium();
    if (key.size() != kKeyBytes)
        return false;
    out.resize(plaintext.size() + kTagBytes);
    unsigned long long written = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            out.data(), &written, plaintext.data(), plaintext.size(), ad.data(),
            ad.size(), nullptr, nonce.data(), key.data()) != 0)
        return false;
    out.resize(static_cast<std::size_t>(written));
    return true;
}

bool aeadOpen(secure::Bytes& out, std::span<const std::uint8_t> ciphertextAndTag,
              std::span<const std::uint8_t, kNonceBytes> nonce,
              std::span<const std::uint8_t> ad, const secure::Bytes& key) {
    secure::detail::ensureSodium();
    if (key.size() != kKeyBytes || ciphertextAndTag.size() < kTagBytes)
        return false;
    out.assign(ciphertextAndTag.size() - kTagBytes, 0);
    unsigned long long written = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            out.data(), &written, nullptr, ciphertextAndTag.data(),
            ciphertextAndTag.size(), ad.data(), ad.size(), nonce.data(),
            key.data()) != 0) {
        secure::zeroize(out.data(), out.size());
        out.clear();
        return false;
    }
    out.resize(static_cast<std::size_t>(written));
    return true;
}

}  // namespace nightlock::crypto
