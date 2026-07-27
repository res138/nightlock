#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace nightlock::io {

// Replaces `path` with head‖tail through the crash-safe protocol from
// docs/format.md: full image into "<path>.tmp" (0600), flush to disk
// (F_FULLFSYNC on macOS), previous file renamed to "<path>.bak", then
// an atomic rename over `path`.
bool atomicReplace(const std::filesystem::path& path,
                   std::span<const std::uint8_t> head,
                   std::span<const std::uint8_t> tail);

}  // namespace nightlock::io
