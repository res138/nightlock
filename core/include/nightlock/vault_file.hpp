#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "crypto.hpp"
#include "group.hpp"
#include "secure.hpp"
#include "vault_errors.hpp"

namespace nightlock {

// One unlocked vault session: owns the decrypted tree and the derived
// key, knows how to persist itself. Move-only; the destructor locks.
//
// Everything the UI holds (Group*/Entry*) points into root(). After
// lock() those pointers dangle — callers clear their views first,
// then lock, and re-read root() after the next open().
class VaultFile {
public:
    // Writes a fresh vault (root group named after the file stem) and
    // returns it open. Creates missing parent directories.
    static Result<VaultFile> create(std::filesystem::path path,
                                    std::string_view password,
                                    const crypto::KdfParams& params = {});

    static Result<VaultFile> open(std::filesystem::path path,
                                  std::string_view password);

    VaultFile(VaultFile&&) noexcept = default;
    VaultFile& operator=(VaultFile&&) noexcept = default;
    VaultFile(const VaultFile&) = delete;
    VaultFile& operator=(const VaultFile&) = delete;
    ~VaultFile();

    // Seals with a fresh nonce and replaces the file atomically; the
    // previous image stays behind as "<path>.bak".
    VaultError save();

    // Writes the live vault to a new path and adopts it as the active
    // file. The original remains untouched; on failure the session
    // keeps its original path.
    VaultError saveAs(std::filesystem::path path);

    // True when `password` derives this open vault's key (same salt
    // and params, constant-time compare). False on a locked vault —
    // there is no key to compare against.
    bool verifyPassword(std::string_view password) const;

    // New salt, new key, immediate save(). The old key is restored if
    // the save fails.
    VaultError changePassword(
        std::string_view newPassword,
        const std::optional<crypto::KdfParams>& newParams = std::nullopt);

    // Wipes every secret in the tree, destroys it and zeroizes the
    // key. Unsaved changes are gone — callers save first.
    void lock();

    bool isOpen() const { return root_ != nullptr; }
    Group* root() { return root_.get(); }
    const Group* root() const { return root_.get(); }
    const std::filesystem::path& path() const { return path_; }
    const crypto::KdfParams& kdfParams() const { return params_; }

private:
    VaultFile() = default;

    std::filesystem::path path_;
    crypto::KdfParams params_;
    std::array<std::uint8_t, crypto::kSaltBytes> salt_{};
    secure::Bytes key_;
    std::unique_ptr<Group> root_;
};

}  // namespace nightlock
