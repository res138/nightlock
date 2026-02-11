#include "Manager.h"
#include <iostream>
#include <variant>
#include "Password.h"
#include "WiFi.h"
#include "Entry.h"

/* Password set in Root/ */
void Manager::root_set_password(
std::string NAME, std::string PASSWORD,
std::optional<std::string> USERNAME, std::optional<std::string> URL,
std::optional<std::string> NOTE) {
    RootEntries.push_back(Password{NAME, PASSWORD, USERNAME, URL, NOTE, std::nullopt, std::nullopt});
}

void Manager::root_set_wifi(
std::string SSID, std::string PASSWORD
) {
    RootEntries.push_back(WiFi{SSID, PASSWORD});
}
