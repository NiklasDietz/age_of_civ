/**
 * @file test_json_parser_depth.cpp
 * @brief Smoke test for JSON parser nesting-depth guard (audit WP3, 2026-05-10).
 *
 * Crafted deeply-nested array / object payloads must yield a null JsonValue
 * (parser bails) instead of recursing past the configured cap and blowing
 * the stack. Real game data nests <10 levels; cap is 64.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/data/JsonParser.hpp"

#include <string>

using aoc::data::JSON_MAX_NESTING_DEPTH;
using aoc::data::JsonValue;
using aoc::data::parseJson;

namespace {

/// Build "[[[...0...]]]" with `depth` opening brackets.
[[nodiscard]] std::string makeNestedArray(int depth) {
    std::string s;
    s.reserve(static_cast<std::size_t>(depth) * 2 + 1);
    for (int i = 0; i < depth; ++i) { s += '['; }
    s += '0';
    for (int i = 0; i < depth; ++i) { s += ']'; }
    return s;
}

/// Build a deeply nested object: { "a": { "a": ... { "a": 0 } ... } }.
[[nodiscard]] std::string makeNestedObject(int depth) {
    std::string s;
    s.reserve(static_cast<std::size_t>(depth) * 8 + 16);
    for (int i = 0; i < depth; ++i) { s += "{\"a\":"; }
    s += '0';
    for (int i = 0; i < depth; ++i) { s += '}'; }
    return s;
}

} // namespace

TEST_CASE("shallow array parses") {
    // 8 levels -- well under cap, must succeed and return an array root.
    std::string payload = makeNestedArray(8);
    JsonValue root = parseJson(payload, "shallow_array");
    CHECK(root.isArray());
}

TEST_CASE("array at exactly the cap parses") {
    std::string payload = makeNestedArray(JSON_MAX_NESTING_DEPTH);
    JsonValue root = parseJson(payload, "at_cap_array");
    CHECK(root.isArray());
}

TEST_CASE("array one past the cap is rejected") {
    // Cap + 1 levels -- parser must bail with null root, not recurse.
    std::string payload = makeNestedArray(JSON_MAX_NESTING_DEPTH + 1);
    JsonValue root = parseJson(payload, "over_cap_array");
    CHECK(root.isNull());
}

TEST_CASE("pathological 10000-deep array is rejected") {
    // A malicious mod payload. Must not crash / recurse 10000 deep.
    std::string payload = makeNestedArray(10000);
    JsonValue root = parseJson(payload, "pathological_array");
    CHECK(root.isNull());
}

TEST_CASE("object one past the cap is rejected") {
    std::string payload = makeNestedObject(JSON_MAX_NESTING_DEPTH + 1);
    JsonValue root = parseJson(payload, "over_cap_object");
    CHECK(root.isNull());
}

TEST_CASE("object at exactly the cap parses") {
    std::string payload = makeNestedObject(JSON_MAX_NESTING_DEPTH);
    JsonValue root = parseJson(payload, "at_cap_object");
    CHECK(root.isObject());
}
