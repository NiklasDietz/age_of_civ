/**
 * @file PathGuard.cpp
 * @brief See PathGuard.hpp.
 */

#include "aoc/core/PathGuard.hpp"

#include <string>
#include <system_error>

namespace aoc::core {

bool isPathInsideAllowlist(
    const std::filesystem::path& candidate,
    const std::vector<std::filesystem::path>& roots) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(candidate, ec);
    if (ec) { return false; }
    for (const std::filesystem::path& root : roots) {
        // Compare component-by-component: candidate must start with the
        // full root prefix. lexically_relative returns a path beginning
        // with ".." when candidate is outside `root`.
        const std::filesystem::path rel = canonical.lexically_relative(root);
        if (rel.empty()) { continue; }
        if (rel.native().rfind("..", 0) == 0) { continue; }
        return true;
    }
    return false;
}

} // namespace aoc::core
