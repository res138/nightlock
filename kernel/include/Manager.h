#pragma once
#include <vector>
#include <variant>
#include <optional>

#include "Entry.h"
#include "Group.h"

class Manager {
    private:
        std::vector<Entry> RootEntries;
        std::vector<Group> RootGroups;
    public:
        void root_set_password(std::string NAME, std::string PASSWORD,
std::optional<std::string> USERNAME, std::optional<std::string> URL, std::optional<std::string> NOTE);
        void root_set_wifi(std::string SSID, std::string PASSWORD);


        template <typename T>
        T* root_get_specific_entry (
            std::string TITLE
        ) {
            for (size_t i = 0; i < RootEntries.size(); i++) {
                if (auto* p = std::get_if<T>(&RootEntries[i])) {
                    return p;
                }
            }
        }

};