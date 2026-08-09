#pragma once

#include <string>
#include <string_view>

namespace aoc::core {

/// Escape `"`, `\`, and control characters for embedding inside a JSON
/// string literal. Control characters (0x00-0x1F) are illegal raw inside
/// a JSON string, so standard short escapes are emitted for `\n \r \t`
/// and `\u00XX` for the rest -- otherwise a strict parser would reject
/// the whole response.
[[nodiscard]] std::string escapeJsonString(std::string_view raw);

} // namespace aoc::core
