#pragma once
#include <cstring>
#include <optional>

struct Password {
    std::string NAME;
    std::string PASSWORD;

    std::optional<std::string> USERNAME;
    std::optional<std::string> URL;
    std::optional<std::string> NOTE;
    std::optional<unsigned int> LAST_MODIFICATION_TIME;
    std::optional<unsigned int> CREATION_TIME;
};