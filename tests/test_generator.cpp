#include <doctest/doctest.h>

#include <nightlock/generator.hpp>

#include <array>
#include <cctype>
#include <map>

using namespace nightlock;

TEST_CASE("length and default charset are honored") {
    GeneratorOptions options;
    options.length = 32;
    const secure::String pw = generatePassword(options);
    REQUIRE(pw.size() == 32);
    for (const char c : pw) {
        const bool ok = std::islower(static_cast<unsigned char>(c)) ||
                        std::isupper(static_cast<unsigned char>(c)) ||
                        std::isdigit(static_cast<unsigned char>(c));
        CHECK(ok);  // symbols are off by default
    }
}

TEST_CASE("single-class pools work") {
    GeneratorOptions options;
    options.upper = options.digits = false;
    options.length = 16;
    const secure::String pw = generatePassword(options);
    REQUIRE(pw.size() == 16);
    for (const char c : pw)
        CHECK(std::islower(static_cast<unsigned char>(c)));
}

TEST_CASE("excluded characters never appear") {
    GeneratorOptions options;
    options.length = 64;
    options.exclude = "0O1lI";
    for (int run = 0; run < 50; ++run) {
        const secure::String pw = generatePassword(options);
        for (const char c : pw)
            CHECK(options.exclude.find(c) == std::string::npos);
    }
}

TEST_CASE("requireEachClass guarantees coverage") {
    GeneratorOptions options;
    options.length = 8;
    options.symbols = true;
    for (int run = 0; run < 200; ++run) {
        const secure::String pw = generatePassword(options);
        bool lower = false, upper = false, digit = false, symbol = false;
        for (const char c : pw) {
            const auto u = static_cast<unsigned char>(c);
            if (std::islower(u))
                lower = true;
            else if (std::isupper(u))
                upper = true;
            else if (std::isdigit(u))
                digit = true;
            else
                symbol = true;
        }
        CHECK(lower);
        CHECK(upper);
        CHECK(digit);
        CHECK(symbol);
    }
}

TEST_CASE("a length below the class count still generates") {
    GeneratorOptions options;
    options.length = 2;  // four classes cannot fit — constraint waived
    options.symbols = true;
    CHECK(generatePassword(options).size() == 2);
}

TEST_CASE("degenerate options return empty") {
    GeneratorOptions options;
    options.lower = options.upper = options.digits = options.symbols = false;
    CHECK(generatePassword(options).empty());

    GeneratorOptions zero;
    zero.length = 0;
    CHECK(generatePassword(zero).empty());

    GeneratorOptions allExcluded;
    allExcluded.upper = allExcluded.digits = false;
    allExcluded.exclude = "abcdefghijklmnopqrstuvwxyz";
    CHECK(generatePassword(allExcluded).empty());
}

TEST_CASE("draws look uniform (loose bound, non-flaky)") {
    GeneratorOptions options;
    options.length = 100;
    options.upper = options.digits = false;  // 26-char pool
    options.requireEachClass = false;

    std::map<char, int> counts;
    for (int run = 0; run < 100; ++run)
        for (const char c : generatePassword(options))
            ++counts[c];
    // 10000 draws over 26 chars ≈ 385 each; flag only gross skew.
    for (const auto& [c, n] : counts) {
        CAPTURE(c);
        CHECK(n > 385 / 3);
        CHECK(n < 385 * 3);
    }
}
