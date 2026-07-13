#pragma once

#include <chrono>
#include <string>

namespace nightlock {

// Classic password entry. name, login, password and both dates are
// required; the rest is optional and empty when unset.
struct Entry {
    std::string name;
    std::string login;
    std::string password;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point modified;

    std::string url;
    std::string icon;  // path or resource id; empty = default icon
    std::string note;
    std::string code;  // 2FA one-time code
};

}  // namespace nightlock
