#include "nightlock/vault_file.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

#include "format/serialize.hpp"
#include "format/tlv.hpp"
#include "vault/atomic_write.hpp"

namespace nightlock {

namespace {

constexpr std::uint8_t kMagic[4] = {'N', 'L', 'C', 'K'};
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::uint16_t kHeaderBytes = 76;

// KDF params sit in the header, readable before any authentication —
// clamp them so a crafted file cannot demand a 128 GiB Argon2 run.
constexpr std::uint32_t kMaxKdfMemoryKiB = 4u * 1024 * 1024;  // 4 GiB
constexpr std::uint32_t kMaxKdfIterations = 64;

bool paramsSane(const crypto::KdfParams& p) {
    return p.memoryKiB > 0 && p.memoryKiB <= kMaxKdfMemoryKiB &&
           p.iterations > 0 && p.iterations <= kMaxKdfIterations &&
           p.parallelism == 1;
}

void appendHeader(secure::Bytes& out, const crypto::KdfParams& params,
                  std::span<const std::uint8_t, crypto::kSaltBytes> salt,
                  std::span<const std::uint8_t, crypto::kNonceBytes> nonce,
                  std::uint64_t ciphertextLen) {
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    format::putU16(out, kFormatVersion);
    format::putU16(out, kHeaderBytes);
    format::putU32(out, crypto::kCipherXChaCha20Poly1305);
    format::putU32(out, crypto::kKdfArgon2id);
    format::putU32(out, params.memoryKiB);
    format::putU32(out, params.iterations);
    format::putU32(out, params.parallelism);
    out.insert(out.end(), salt.begin(), salt.end());
    out.insert(out.end(), nonce.begin(), nonce.end());
    format::putU64(out, ciphertextLen);
}

void wipeTree(Group& group) {
    for (const auto& entry : group.entries()) {
        secure::wipe(entry->password);
        secure::wipe(entry->code);
        for (EntryField& field : entry->fields)
            secure::wipe(field.value);
    }
    for (const auto& child : group.groups())
        wipeTree(*child);
}

}  // namespace

const char* errorMessage(VaultError error) {
    switch (error) {
        case VaultError::None: return "no error";
        case VaultError::IoError: return "file could not be read or written";
        case VaultError::NotAVault: return "not a nightlock vault";
        case VaultError::UnsupportedVersion:
            return "vault was written by a newer nightlock";
        case VaultError::InvalidHeader: return "vault header is invalid";
        case VaultError::WrongPassword:
            return "wrong password — or the file is damaged";
        case VaultError::Corrupt: return "vault payload is corrupt";
        case VaultError::NotOpen: return "vault is locked";
    }
    return "unknown error";
}

VaultFile::~VaultFile() { lock(); }

void VaultFile::lock() {
    if (root_) {
        wipeTree(*root_);
        root_.reset();
    }
    if (!key_.empty()) {
        secure::zeroize(key_.data(), key_.size());
        key_.clear();
        key_.shrink_to_fit();
    }
}

Result<VaultFile> VaultFile::create(std::filesystem::path path,
                                    std::string_view password,
                                    const crypto::KdfParams& params) {
    if (!paramsSane(params))
        return VaultError::InvalidHeader;

    std::error_code ec;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), ec);

    VaultFile vault;
    vault.path_ = std::move(path);
    vault.params_ = params;
    crypto::randomBytes(vault.salt_.data(), vault.salt_.size());
    if (!crypto::deriveKey(vault.key_, password, vault.salt_, params))
        return VaultError::IoError;
    vault.root_ = std::make_unique<Group>(vault.path_.stem().string());

    if (const VaultError error = vault.save(); error != VaultError::None)
        return error;
    return vault;
}

Result<VaultFile> VaultFile::open(std::filesystem::path path,
                                  std::string_view password) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return VaultError::IoError;
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (in.bad())
        return VaultError::IoError;

    if (file.size() < sizeof(kMagic) ||
        !std::equal(std::begin(kMagic), std::end(kMagic), file.begin()))
        return VaultError::NotAVault;
    if (file.size() < kHeaderBytes)
        return VaultError::InvalidHeader;

    const std::span<const std::uint8_t> header(file.data(), kHeaderBytes);
    if (format::getU16(header.subspan(4)) != kFormatVersion)
        return VaultError::UnsupportedVersion;
    if (format::getU16(header.subspan(6)) != kHeaderBytes)
        return VaultError::InvalidHeader;
    if (format::getU32(header.subspan(8)) != crypto::kCipherXChaCha20Poly1305 ||
        format::getU32(header.subspan(12)) != crypto::kKdfArgon2id)
        return VaultError::UnsupportedVersion;

    crypto::KdfParams params;
    params.memoryKiB = format::getU32(header.subspan(16));
    params.iterations = format::getU32(header.subspan(20));
    params.parallelism = format::getU32(header.subspan(24));
    if (!paramsSane(params))
        return VaultError::InvalidHeader;

    if (format::getU64(header.subspan(68)) != file.size() - kHeaderBytes ||
        file.size() - kHeaderBytes < crypto::kTagBytes)
        return VaultError::InvalidHeader;

    VaultFile vault;
    vault.path_ = std::move(path);
    vault.params_ = params;
    std::copy_n(header.subspan(28).begin(), vault.salt_.size(),
                vault.salt_.begin());
    std::array<std::uint8_t, crypto::kNonceBytes> nonce;
    std::copy_n(header.subspan(44).begin(), nonce.size(), nonce.begin());

    if (!crypto::deriveKey(vault.key_, password, vault.salt_, params))
        return VaultError::IoError;

    secure::Bytes payload;
    if (!crypto::aeadOpen(payload,
                          std::span(file).subspan(kHeaderBytes), nonce,
                          header, vault.key_))
        return VaultError::WrongPassword;

    switch (format::deserializeVault(payload, vault.root_)) {
        case format::ParseResult::Ok: break;
        case format::ParseResult::Malformed: return VaultError::Corrupt;
        case format::ParseResult::Unsupported:
            return VaultError::UnsupportedVersion;
    }
    return vault;
}

VaultError VaultFile::save() {
    if (!root_)
        return VaultError::NotOpen;

    secure::Bytes payload;
    format::serializeVault(*root_, payload);

    std::array<std::uint8_t, crypto::kNonceBytes> nonce;
    crypto::randomBytes(nonce.data(), nonce.size());

    secure::Bytes header;
    appendHeader(header, params_, salt_, nonce,
                 payload.size() + crypto::kTagBytes);

    std::vector<std::uint8_t> sealed;
    if (!crypto::aeadSeal(sealed, payload, nonce, header, key_))
        return VaultError::IoError;

    if (!io::atomicReplace(path_, header, sealed))
        return VaultError::IoError;
    return VaultError::None;
}

VaultError VaultFile::saveAs(std::filesystem::path path) {
    if (!root_)
        return VaultError::NotOpen;
    if (path == path_)
        return save();

    const std::filesystem::path previous = path_;
    path_ = std::move(path);
    const VaultError error = save();
    if (error != VaultError::None)
        path_ = previous;
    return error;
}

bool VaultFile::verifyPassword(std::string_view password) const {
    if (!root_ || key_.empty())
        return false;
    secure::Bytes candidate;
    if (!crypto::deriveKey(candidate, password, salt_, params_))
        return false;
    const bool match = crypto::secretEquals(candidate, key_);
    secure::zeroize(candidate.data(), candidate.size());
    return match;
}

VaultError VaultFile::changePassword(
    std::string_view newPassword,
    const std::optional<crypto::KdfParams>& newParams) {
    if (!root_)
        return VaultError::NotOpen;

    const crypto::KdfParams params = newParams.value_or(params_);
    if (!paramsSane(params))
        return VaultError::InvalidHeader;

    // Stage the new identity, keep the old one for rollback.
    const crypto::KdfParams oldParams = params_;
    const auto oldSalt = salt_;
    secure::Bytes oldKey = std::move(key_);

    params_ = params;
    crypto::randomBytes(salt_.data(), salt_.size());
    if (!crypto::deriveKey(key_, newPassword, salt_, params_)) {
        params_ = oldParams;
        salt_ = oldSalt;
        key_ = std::move(oldKey);
        return VaultError::IoError;
    }

    if (const VaultError error = save(); error != VaultError::None) {
        params_ = oldParams;
        salt_ = oldSalt;
        key_ = std::move(oldKey);
        return error;
    }
    secure::zeroize(oldKey.data(), oldKey.size());
    return VaultError::None;
}

}  // namespace nightlock
