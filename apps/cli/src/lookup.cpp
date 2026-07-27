#include "lookup.hpp"

#include <string>

namespace cli {

namespace {

nightlock::Group* childNamed(nightlock::Group& parent, std::string_view name) {
    for (const auto& child : parent.groups())
        if (child->name() == name)
            return child.get();
    return nullptr;
}

}  // namespace

std::vector<std::string_view> splitPath(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        if (!part.empty())
            parts.push_back(part);
        if (slash == std::string_view::npos)
            break;
        path.remove_prefix(slash + 1);
    }
    return parts;
}

nightlock::Group* findGroup(nightlock::Group& root, std::string_view path) {
    nightlock::Group* current = &root;
    for (const std::string_view part : splitPath(path)) {
        current = childNamed(*current, part);
        if (!current)
            return nullptr;
    }
    return current;
}

EntryRef findEntry(nightlock::Group& root, std::string_view path) {
    const std::vector<std::string_view> parts = splitPath(path);
    if (parts.empty())
        return {};

    nightlock::Group* parent = &root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        parent = childNamed(*parent, parts[i]);
        if (!parent)
            return {};
    }
    for (const auto& entry : parent->entries())
        if (entry->name == parts.back())
            return {parent, entry.get()};
    return {};
}

nightlock::Group& ensureGroup(nightlock::Group& root, std::string_view path) {
    nightlock::Group* current = &root;
    for (const std::string_view part : splitPath(path)) {
        nightlock::Group* next = childNamed(*current, part);
        current = next ? next : &current->addGroup(std::string(part));
    }
    return *current;
}

}  // namespace cli
