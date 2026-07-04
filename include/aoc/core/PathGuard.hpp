#pragma once

/**
 * @file PathGuard.hpp
 * @brief Allowlist containment check for externally supplied file paths.
 *
 * Shared by every surface that writes files on behalf of an external
 * caller (DBus screenshot requests, HTTP debug dump routes). Compiled
 * unconditionally -- do not gate this behind optional-feature defines.
 */

#include <filesystem>
#include <vector>

namespace aoc::core {

/// @brief True iff `candidate` lies inside any allowlisted root after
///        symlink resolution.
///
/// Uses `weakly_canonical` so the target file does not need to exist yet
/// (callers are about to create it); every EXISTING path component has
/// its symlinks resolved before the check. Containment is decided by
/// component-wise prefix comparison of the canonicalised paths.
///
/// @param candidate Path to test. May be relative; it is canonicalised
///                  against the current working directory.
/// @param roots     Allowed root directories, already canonicalised.
/// @return true when `candidate` resolves under at least one root.
[[nodiscard]] bool isPathInsideAllowlist(
    const std::filesystem::path& candidate,
    const std::vector<std::filesystem::path>& roots);

} // namespace aoc::core
