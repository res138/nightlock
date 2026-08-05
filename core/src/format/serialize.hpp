#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "nightlock/group.hpp"
#include "nightlock/secure.hpp"

// Vault payload <-> Group tree. The payload is what gets AEAD-sealed
// into the .nlck file; docs/format.md is the normative spec.

namespace nightlock::format {

inline constexpr std::uint32_t kPayloadVersion = 1;

// Payload tags. Bit 0x8000 = critical (see tlv.hpp).
inline constexpr std::uint16_t kTagMeta = 0x0001;
inline constexpr std::uint16_t kTagPayloadVersion = 0x8101;
inline constexpr std::uint16_t kTagVaultName = 0x0102;
inline constexpr std::uint16_t kTagSavedAt = 0x0103;
inline constexpr std::uint16_t kTagGroup = 0x0002;
inline constexpr std::uint16_t kTagGroupName = 0x0201;
inline constexpr std::uint16_t kTagGroupIcon = 0x0202;
inline constexpr std::uint16_t kTagEntry = 0x0003;
inline constexpr std::uint16_t kTagEntryName = 0x0301;
inline constexpr std::uint16_t kTagEntryLogin = 0x0302;
inline constexpr std::uint16_t kTagEntryPassword = 0x0303;
inline constexpr std::uint16_t kTagEntryCreatedMs = 0x0304;
inline constexpr std::uint16_t kTagEntryModifiedMs = 0x0305;
inline constexpr std::uint16_t kTagEntryUrl = 0x0306;
inline constexpr std::uint16_t kTagEntryIcon = 0x0307;
inline constexpr std::uint16_t kTagEntryNote = 0x0308;
inline constexpr std::uint16_t kTagEntryCode = 0x0309;
inline constexpr std::uint16_t kTagEntryPattern = 0x030A;
inline constexpr std::uint16_t kTagEntryPreset = 0x030B;
inline constexpr std::uint16_t kTagEntryField = 0x030C;
inline constexpr std::uint16_t kTagFieldLabel = 0x0C01;
inline constexpr std::uint16_t kTagFieldValue = 0x0C02;
inline constexpr std::uint16_t kTagFieldSecret = 0x0C03;
inline constexpr std::uint16_t kTagFieldCustom = 0x0C04;

enum class ParseResult {
    Ok,
    Malformed,    // truncated/overflowing records, missing root, bad field size
    Unsupported,  // newer PayloadVersion or an unknown critical tag
};

// Appends one Meta record and one root Group record to `out`.
void serializeVault(const Group& root, secure::Bytes& out);

// On Ok, `rootOut` holds the rebuilt tree (child order preserved — it
// is the user's custom sort order).
ParseResult deserializeVault(std::span<const std::uint8_t> payload,
                             std::unique_ptr<Group>& rootOut);

}  // namespace nightlock::format
