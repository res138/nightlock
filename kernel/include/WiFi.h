#pragma once
#include <cstring>
#include <optional>

struct WiFi {
    std::string NAME; /* SSID */
    std::optional<std::string> BSSID;
    std::string PASSWORD;
};