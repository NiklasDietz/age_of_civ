/**
 * @file JsonUtil.cpp
 * @brief See JsonUtil.hpp.
 */

#include "aoc/core/JsonUtil.hpp"

namespace aoc::core {

std::string escapeJsonString(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    static constexpr char kHex[] = "0123456789abcdef";
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += kHex[(c >> 4) & 0x0F];
                out += kHex[c & 0x0F];
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

} // namespace aoc::core
