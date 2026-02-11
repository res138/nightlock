#pragma once
#include <variant>
#include "Password.h"
#include "WiFi.h"

using Entry = std::variant<Password, WiFi>;
