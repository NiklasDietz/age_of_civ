/**
 * @file test_json_parser_depth.cpp
 * @brief Smoke test for JSON parser nesting-depth guard (audit WP3, 2026-05-10).
 *
 * Crafted deeply-nested array / object payloads must yield a null JsonValue
 * (parser bails) instead of recursing past the configured cap and blowing
 * the stack. Real game data nests <10 levels; cap is 64.
 */

#include "aoc/data/JsonParser.hpp"

#include <cassert>
#include <cstdio>
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

void test_shallowArrayParses() {
    // 8 levels -- well under cap, must succeed and return an array root.
    std::string payload = makeNestedArray(8);
    JsonValue root = parseJson(payload, "shallow_array");
    assert(root.isArray());
}

void test_atCapArrayParses() {
    // Exactly cap levels -- must still succeed.
    std::string payload = makeNestedArray(JSON_MAX_NESTING_DEPTH);
    JsonValue root = parseJson(payload, "at_cap_array");
    assert(root.isArray());
}

void test_overCapArrayRejected() {
    // Cap + 1 levels -- parser must bail with null root, not recurse.
    std::string payload = makeNestedArray(JSON_MAX_NESTING_DEPTH + 1);
    JsonValue root = parseJson(payload, "over_cap_array");
    assert(root.isNull());
}

void test_pathologicalArrayRejected() {
    // Far past the cap -- a malicious mod payload. Must not crash / recurse
    // 10000 deep; must return null.
    std::string payload = makeNestedArray(10000);
    JsonValue root = parseJson(payload, "pathological_array");
    assert(root.isNull());
}

void test_overCapObjectRejected() {
    std::string payload = makeNestedObject(JSON_MAX_NESTING_DEPTH + 1);
    JsonValue root = parseJson(payload, "over_cap_object");
    assert(root.isNull());
}

void test_atCapObjectParses() {
    std::string payload = makeNestedObject(JSON_MAX_NESTING_DEPTH);
    JsonValue root = parseJson(payload, "at_cap_object");
    assert(root.isObject());
}

} // namespace

int main() {
    test_shallowArrayParses();
    test_atCapArrayParses();
    test_overCapArrayRejected();
    test_pathologicalArrayRejected();
    test_overCapObjectRejected();
    test_atCapObjectParses();
    std::printf("test_json_parser_depth: all passed\n");
    return 0;
}
