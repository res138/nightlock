#pragma once
#include <cstring>
#include <vector>
#include "Entry.h"


struct Group {
    std::string NAME;
    std::vector<Entry> Entries;
    std::vector<Group> SubGroups;
};