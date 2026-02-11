#pragma once
#include <cstring>
#include <optional>

struct WiFi {
    std::string SSID;
    std::optional<std::string> BSSID;
    std::string PASSWORD;
};