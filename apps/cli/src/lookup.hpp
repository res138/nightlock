#pragma once

#include <string_view>
#include <vector>

#include <nightlock/group.hpp>

namespace cli {

// Vault paths are '/'-separated and relative to the root group:
// "Work/Banking/GitHub". Empty components are dropped, so "/" and ""
// both mean the root. Name collisions resolve to the first match in
// stored order.

std::vector<std::string_view> splitPath(std::string_view path);

// nullptr when any component is missing.
nightlock::Group* findGroup(nightlock::Group& root, std::string_view path);

struct EntryRef {
    nightlock::Group* group = nullptr;
    nightlock::Entry* entry = nullptr;
};

// The last component names the entry, the prefix its folder chain.
// {nullptr, nullptr} when not found.
EntryRef findEntry(nightlock::Group& root, std::string_view path);

// findGroup that creates missing components along the way (mkdir -p).
nightlock::Group& ensureGroup(nightlock::Group& root, std::string_view path);

}  // namespace cli
