#include <nightlock/expiration.hpp>

#include <charconv>

namespace nightlock::expiration {
namespace {

std::optional<unsigned> number(std::string_view value) noexcept {
    unsigned result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return std::nullopt;
    return result;
}

}  // namespace

bool isLabel(std::string_view label) noexcept {
    return label == "expiration" || label == "Expiration" || label == "EXPIRATION";
}

std::optional<std::chrono::year_month_day> parseDate(std::string_view value) noexcept {
    if (value.size() != 10 || value[2] != '/' || value[5] != '/')
        return std::nullopt;

    const auto day = number(value.substr(0, 2));
    const auto month = number(value.substr(3, 2));
    const auto year = number(value.substr(6, 4));
    if (!day || !month || !year)
        return std::nullopt;

    const std::chrono::year_month_day date{
        std::chrono::year(static_cast<int>(*year)),
        std::chrono::month(*month),
        std::chrono::day(*day),
    };
    if (!date.ok())
        return std::nullopt;
    return date;
}

const EntryField* field(const Entry& entry) noexcept {
    for (const EntryField& candidate : entry.fields)
        if (candidate.custom && isLabel(candidate.label) && !candidate.value.empty())
            return &candidate;
    return nullptr;
}

bool isExpired(const Entry& entry, std::chrono::year_month_day today) noexcept {
    const EntryField* expirationField = field(entry);
    if (!expirationField)
        return false;
    const auto expirationDate = parseDate(secure::view(expirationField->value));
    return expirationDate && std::chrono::sys_days(today) >= std::chrono::sys_days(*expirationDate);
}

}  // namespace nightlock::expiration
