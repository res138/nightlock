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
        void root_set_wifi(std::string NAME, std::string PASSWORD);

        template <typename T>
        T* root_get_specific_entry (
            std::string TITLE
        ) {
            for (size_t i = 0; i < RootEntries.size(); i++) {
                if (auto* p = std::get_if<T>(&RootEntries[i])) {
                    if (p->NAME == TITLE) {
                        return p;
                    }
                }
            }
            return nullptr;
        }

        template <typename T>
        size_t root_get_specific_entry_index (
            std::string TITLE
        ) {
            for (size_t i = 0; i < RootEntries.size(); i++) {
                if (auto* p = std::get_if<T>(&RootEntries[i])) {
                    if (p->NAME == TITLE) {
                        return i;
                    }
                }
            }
            return -1;
        }

        template <typename T>
        void root_delete_specific_entry (
            std::string TITLE
        ) {
            size_t index = this->template root_get_specific_entry_index<T>(TITLE);

            // O(n) difficulty
            if (index >= 0 )
            RootEntries.erase(RootEntries.begin() + index);

        }

};