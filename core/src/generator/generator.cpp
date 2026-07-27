#include "nightlock/generator.hpp"

#include <array>
#include <string_view>
#include <vector>

#include "nightlock/crypto.hpp"

namespace nightlock {

namespace {

constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
constexpr std::string_view kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::string_view kDigits = "0123456789";
constexpr std::string_view kSymbols = "!#$%&()*+,-./:;<=>?@[]^_{|}~";

struct Pool {
    secure::String chars;                 // the merged draw pool
    std::vector<secure::String> classes;  // per enabled class, post-exclude
};

Pool buildPool(const GeneratorOptions& options) {
    Pool pool;
    const std::array<std::pair<bool, std::string_view>, 4> classes = {{
        {options.lower, kLower},
        {options.upper, kUpper},
        {options.digits, kDigits},
        {options.symbols, kSymbols},
    }};
    for (const auto& [enabled, alphabet] : classes) {
        if (!enabled)
            continue;
        secure::String kept;
        for (const char c : alphabet)
            if (options.exclude.find(c) == std::string::npos)
                kept.push_back(c);
        if (kept.empty())
            continue;  // class excluded away entirely
        pool.chars += kept;
        pool.classes.push_back(std::move(kept));
    }
    return pool;
}

bool coversEveryClass(const secure::String& candidate, const Pool& pool) {
    for (const auto& cls : pool.classes) {
        bool hit = false;
        for (const char c : candidate)
            if (cls.find(c) != secure::String::npos) {
                hit = true;
                break;
            }
        if (!hit)
            return false;
    }
    return true;
}

}  // namespace

secure::String generatePassword(const GeneratorOptions& options) {
    if (options.length <= 0)
        return {};
    const Pool pool = buildPool(options);
    if (pool.chars.empty())
        return {};

    const bool enforceClasses =
        options.requireEachClass &&
        static_cast<std::size_t>(options.length) >= pool.classes.size();

    // Rejection sampling keeps the distribution uniform over all
    // strings that satisfy the class constraint; for length >= 8 the
    // expected number of attempts is ~1.
    secure::String candidate;
    do {
        candidate.clear();
        for (int i = 0; i < options.length; ++i)
            candidate.push_back(pool.chars[crypto::randomUniform(
                static_cast<std::uint32_t>(pool.chars.size()))]);
    } while (enforceClasses && !coversEveryClass(candidate, pool));
    return candidate;
}

}  // namespace nightlock
