#include "nightlock/group.hpp"

#include <algorithm>

namespace nightlock {

Group::Group(std::string name, Group* parent)
    : name_(std::move(name)), parent_(parent) {}

const std::string& Group::name() const { return name_; }
void Group::setName(std::string name) { name_ = std::move(name); }

const std::string& Group::icon() const { return icon_; }
void Group::setIcon(std::string icon) { icon_ = std::move(icon); }

Group* Group::parent() const { return parent_; }

Group& Group::addGroup(std::string name) {
    groups_.push_back(std::make_unique<Group>(std::move(name), this));
    return *groups_.back();
}

Entry& Group::addEntry(Entry entry) {
    entries_.push_back(std::make_unique<Entry>(std::move(entry)));
    return *entries_.back();
}

bool Group::removeGroup(Group* child) {
    const auto it = std::find_if(groups_.begin(), groups_.end(),
                                 [child](const auto& p) { return p.get() == child; });
    if (it == groups_.end())
        return false;
    groups_.erase(it);
    return true;
}

bool Group::removeEntry(Entry* entry) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [entry](const auto& p) { return p.get() == entry; });
    if (it == entries_.end())
        return false;
    entries_.erase(it);
    return true;
}

bool Group::moveEntry(int from, int to) {
    const int count = static_cast<int>(entries_.size());
    if (from < 0 || from >= count)
        return false;
    if (to < 0 || to > count)
        to = count;
    if (to == from || to == from + 1)
        return false;

    std::unique_ptr<Entry> holder = std::move(entries_[from]);
    entries_.erase(entries_.begin() + from);
    entries_.insert(entries_.begin() + (to > from ? to - 1 : to), std::move(holder));
    return true;
}

const std::vector<std::unique_ptr<Group>>& Group::groups() const { return groups_; }
const std::vector<std::unique_ptr<Entry>>& Group::entries() const { return entries_; }

int Group::indexInParent() const {
    if (!parent_)
        return 0;
    const auto& siblings = parent_->groups_;
    for (std::size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == this)
            return static_cast<int>(i);
    return 0;
}

std::string Group::path(char separator) const {
    if (!parent_)
        return name_;
    return parent_->path(separator) + separator + name_;
}

bool Group::isAncestorOf(const Group* other) const {
    for (const Group* p = other ? other->parent_ : nullptr; p; p = p->parent_)
        if (p == this)
            return true;
    return false;
}

bool Group::reparent(Group& child, Group& newParent, int position) {
    Group* oldParent = child.parent_;
    if (!oldParent || &child == &newParent || child.isAncestorOf(&newParent))
        return false;

    auto& source = oldParent->groups_;
    const auto it = std::find_if(source.begin(), source.end(),
                                 [&child](const auto& p) { return p.get() == &child; });
    if (it == source.end())
        return false;

    const int oldIndex = static_cast<int>(it - source.begin());
    std::unique_ptr<Group> holder = std::move(*it);
    source.erase(it);

    auto& destination = newParent.groups_;
    int index = position;
    if (oldParent == &newParent && position > oldIndex)
        --index;
    if (index < 0 || index > static_cast<int>(destination.size()))
        index = static_cast<int>(destination.size());
    destination.insert(destination.begin() + index, std::move(holder));
    child.parent_ = &newParent;
    return true;
}

}  // namespace nightlock
