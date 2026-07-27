#pragma once

#include <optional>
#include <utility>

namespace nightlock {

enum class VaultError {
    None = 0,
    IoError,             // open/read/write/flush/rename failed
    NotAVault,           // bad magic
    UnsupportedVersion,  // unknown format version, cipher, kdf or critical tag
    InvalidHeader,       // truncated header, size mismatch, absurd KDF params
    WrongPassword,       // AEAD auth failure — also covers tampering
    Corrupt,             // decrypted fine, TLV payload malformed
    NotOpen,             // operation on a locked/moved-from vault
};

// Short human-readable description for CLI/log output. (Deliberately
// not named toString — that is doctest's ADL customization point.)
const char* errorMessage(VaultError error);

// Minimal expected<T, VaultError>; std::expected is C++23 and the
// Group API next door is exception-free, so vault APIs are too.
template <class T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(VaultError error) : error_(error) {}

    explicit operator bool() const { return value_.has_value(); }
    VaultError error() const { return error_; }

    T& value() { return *value_; }
    const T& value() const { return *value_; }
    T&& take() { return std::move(*value_); }

private:
    std::optional<T> value_;
    VaultError error_ = VaultError::None;
};

}  // namespace nightlock
