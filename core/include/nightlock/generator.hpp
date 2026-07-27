#pragma once

#include <string>

#include "secure.hpp"

namespace nightlock {

struct GeneratorOptions {
    int length = 20;
    bool lower = true;
    bool upper = true;
    bool digits = true;
    bool symbols = false;
    // Guarantee at least one character from every enabled class (only
    // enforced when length allows it).
    bool requireEachClass = true;
    // Characters to drop from the pool, e.g. "0O1lI" for readability.
    std::string exclude;
};

// Empty result when the options leave no characters to draw from or
// the length is not positive. Uniform, CSPRNG-backed, unbiased.
secure::String generatePassword(const GeneratorOptions& options);

}  // namespace nightlock
