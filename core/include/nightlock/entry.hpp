#pragma once

#include <chrono>
#include <string>

namespace nightlock {

// Decorative background pattern behind the entry icon in the detail
// panel. Off by default; the user opts in per entry when creating or
// editing it. The pattern itself is never stored — it is regenerated
// from the entry's icon palette and creation date.
enum class Pattern {
    None = 0,
    GlowSoft,    // 5 soft radial color blobs, barely-there alpha
    GlowBold,    // same blobs, twice the presence
    IconTile,    // v1: the icon itself, tiled with jittered rotation
    IconTileV2,  // v2: tiled straight, no rotation
    IconTileV3,  // v3: straight and each tile 0.70–1.4× the base size
    Ripple,      // concentric rings radiating from the icon center
};

// Classic password entry. name, login, password and both dates are
// required; the rest is optional and empty when unset.
struct Entry {
    std::string name;
    std::string login;
    std::string password;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point modified;

    std::string url;
    std::string icon;  // path or resource id; empty = default icon
    std::string note;
    std::string code;  // 2FA one-time code
    Pattern pattern = Pattern::None;  // detail-view background pattern
};

}  // namespace nightlock
