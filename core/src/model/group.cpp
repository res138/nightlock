#include "nightlock/group.hpp"

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

}  // namespace nightlock
