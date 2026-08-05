#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "secure.hpp"

namespace nightlock {

// Decorative background pattern behind the entry icon in the detail
// panel. Off by default; the user opts in per entry when creating or
// editing it. The pattern itself is never stored — it is regenerated
// from the entry's icon palette and creation date.
enum class Pattern {
    None = 0,
    GlowSoft,    // 5 soft radial color blobs, barely-there alpha
    GlowBold,    // same blobs, twice the presence
    IconTile,       // v1: the icon itself, tiled with jittered rotation
    IconTileV2,     // v2: tiled straight, no rotation
    IconTileV3,     // v3: straight and each tile 0.70–1.4× the base size
    Ripple,         // concentric rings radiating from the icon center
    Constellation,  // dots linked to their two nearest neighbors
    Aurora,         // wide bezier ribbons flowing across the zone
    Halo,           // halftone dot grid peaking on a ring around the icon
};

// The form used when an entry was created. Classic keeps the original
// Nightlock fields; the other presets only change labels and add a
// small schema of extra fields. The selector itself is Add-only, but
// the id is stored so Edit and the detail viewer keep the right labels.
enum class EntryPreset {
    Classic = 0,
    Wifi,
    BankCard,
    BrowserBookmark,
    CryptoWallet,
};

// One preset-defined or user-defined field. Values use secure storage
// because a custom field can contain secrets even when its current
// display mode is plain text.
struct EntryField {
    std::string label;
    secure::String value;
    bool secret = false;
    bool custom = false;

    bool operator==(const EntryField&) const = default;
};

// Password-manager entry. Name and dates form the common core; a
// preset gives the classic fields contextual labels and may add more
// fields. Secret values live in secure::String — pinned pages and
// explicitly wiped when the vault locks.
struct Entry {
    std::string name;
    std::string login;
    secure::String password;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point modified;

    std::string url;
    std::string icon;  // path or resource id; empty = default icon
    std::string note;
    secure::String code;  // 2FA one-time code
    Pattern pattern = Pattern::None;  // detail-view background pattern
    EntryPreset preset = EntryPreset::Classic;
    std::vector<EntryField> fields;
};

}  // namespace nightlock
