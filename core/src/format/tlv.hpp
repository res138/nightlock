#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "nightlock/secure.hpp"

// Record = tag:u16 | length:u32 | value, little-endian, values nest.
// Tag bit 0x8000 marks a record as critical: readers skip unknown
// non-critical tags (additive format evolution without a version
// bump) and must reject files carrying unknown critical ones.
//
// Internal header — the public surface is VaultFile; tests include
// this via the core/src include path.

namespace nightlock::format {

inline constexpr std::uint16_t kCriticalTagBit = 0x8000;
inline constexpr std::size_t kRecordHeadBytes = 6;

// Explicit byte shuffles, not struct memcpy: identical bytes on every
// platform the Windows/Linux ports will meet.
void putU16(secure::Bytes& out, std::uint16_t v);
void putU32(secure::Bytes& out, std::uint32_t v);
void putU64(secure::Bytes& out, std::uint64_t v);
std::uint16_t getU16(std::span<const std::uint8_t> in) noexcept;  // in.size() >= 2
std::uint32_t getU32(std::span<const std::uint8_t> in) noexcept;  // in.size() >= 4
std::uint64_t getU64(std::span<const std::uint8_t> in) noexcept;  // in.size() >= 8

// Appends records to a caller-owned buffer (secure::Bytes, so vault
// plaintext never touches an unscrubbed allocation).
class TlvWriter {
public:
    explicit TlvWriter(secure::Bytes& out) : out_(out) {}

    void bytes(std::uint16_t tag, std::span<const std::uint8_t> value);
    void string(std::uint16_t tag, std::string_view value);
    void u32(std::uint16_t tag, std::uint32_t value);
    void i64(std::uint16_t tag, std::int64_t value);

    // Opens a container record; the returned token patches the length
    // in endContainer. Nesting is fine as long as the calls pair up
    // like brackets.
    std::size_t beginContainer(std::uint16_t tag);
    void endContainer(std::size_t token);

private:
    secure::Bytes& out_;
};

// Bounds-checked cursor over one flat record sequence. Call next()
// before the accessors; a record whose length overruns the buffer
// stops iteration and raises malformed().
class TlvReader {
public:
    explicit TlvReader(std::span<const std::uint8_t> data) : data_(data) {}

    bool next() noexcept;
    bool malformed() const noexcept { return malformed_; }

    std::uint16_t tag() const noexcept { return tag_; }
    std::span<const std::uint8_t> value() const noexcept { return value_; }
    std::string_view valueString() const noexcept;
    std::optional<std::uint32_t> valueU32() const noexcept;  // nullopt on wrong size
    std::optional<std::int64_t> valueI64() const noexcept;
    TlvReader container() const noexcept { return TlvReader(value_); }

private:
    std::span<const std::uint8_t> data_;
    std::size_t cursor_ = 0;
    std::uint16_t tag_ = 0;
    std::span<const std::uint8_t> value_;
    bool malformed_ = false;
};

}  // namespace nightlock::format
