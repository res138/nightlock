#pragma once

#include <memory>
#include <string>
#include <vector>

#include "entry.hpp"

namespace nightlock {

// A directory in the vault: holds entries and nested sub-groups.
class Group {
public:
    explicit Group(std::string name, Group* parent = nullptr);

    const std::string& name() const;
    void setName(std::string name);

    const std::string& icon() const;
    void setIcon(std::string icon);

    Group* parent() const;

    Group& addGroup(std::string name);
    Entry& addEntry(Entry entry);

    // Destroys `child` (with its whole subtree). Fails when `child` is
    // not a direct sub-group.
    bool removeGroup(Group* child);

    // Destroys `entry`. Fails when it does not belong to this group.
    bool removeEntry(Entry* entry);

    // Moves the entry at `from` to `to` (pre-removal index semantics,
    // like reparent()).
    bool moveEntry(int from, int to);

    // Moves `entry` to the end of `target`, keeping the object (and
    // pointers to it) alive. Fails when it does not belong to this
    // group; moving into the same group is a no-op success.
    bool transferEntry(Entry* entry, Group& target);

    const std::vector<std::unique_ptr<Group>>& groups() const;
    const std::vector<std::unique_ptr<Entry>>& entries() const;

    int indexInParent() const;
    std::string path(char separator = '/') const;  // "Root/Personal 2020"

    bool isAncestorOf(const Group* other) const;

    // Moves `child` out of its current parent into `newParent` at
    // `position` (pre-removal index semantics when moving within the
    // same parent; clamped to the end otherwise). Fails on cycles.
    static bool reparent(Group& child, Group& newParent, int position = -1);

private:
    std::string name_;
    std::string icon_;
    Group* parent_ = nullptr;
    std::vector<std::unique_ptr<Group>> groups_;
    std::vector<std::unique_ptr<Entry>> entries_;
};

}  // namespace nightlock
