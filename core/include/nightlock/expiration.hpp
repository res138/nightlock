#pragma once

#include <chrono>
#include <optional>
#include <string_view>

#include "entry.hpp"

namespace nightlock::expiration {

// Reserved custom-field labels. Deliberately limited to the three
// documented spellings so similarly named user fields keep their
// ordinary custom-field behavior.
bool isLabel(std::string_view label) noexcept;

// Expiration values are persisted in the locale-independent
// DD/MM/YYYY form. Presentation is the desktop application's job.
std::optional<std::chrono::year_month_day> parseDate(std::string_view value) noexcept;

// Returns the first non-empty reserved custom field, if one exists.
const EntryField* field(const Entry& entry) noexcept;

// An entry expires on the specified date, not after that day ends.
bool isExpired(const Entry& entry, std::chrono::year_month_day today) noexcept;

}  // namespace nightlock::expiration
