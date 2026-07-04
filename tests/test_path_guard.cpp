/**
 * @file test_path_guard.cpp
 * @brief Smoke test for aoc::core::isPathInsideAllowlist (2026-06-06 audit,
 *        debug-server hardening work package).
 *
 * The predicate guards every externally supplied write path (DBus
 * screenshots, HTTP /dump routes). It must accept paths inside an
 * allowlisted root -- including not-yet-existing leaves -- and reject
 * traversal (`..`), sibling-prefix confusion (`/root-evil` vs `/root`),
 * and symlinks escaping the root.
 *
 * Checks use an explicit helper instead of assert(): the release test
 * gate compiles with -DNDEBUG, which would silence assert entirely.
 */

#include "aoc/core/PathGuard.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

/// Fresh unique scratch directory under the system temp dir.
[[nodiscard]] std::filesystem::path makeScratchDir() {
    std::filesystem::path base =
        std::filesystem::temp_directory_path() / "aoc_path_guard_test";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return std::filesystem::weakly_canonical(base);
}

} // namespace

int main() {
    const std::filesystem::path scratch = makeScratchDir();
    const std::filesystem::path root = scratch / "dumps";
    std::filesystem::create_directories(root);
    const std::vector<std::filesystem::path> roots{
        std::filesystem::weakly_canonical(root)};

    // Inside the root: existing dir, and a leaf that does not exist yet.
    check(aoc::core::isPathInsideAllowlist(root / "a.csv", roots),
          "existing-dir leaf accepted");
    check(aoc::core::isPathInsideAllowlist(root / "sub" / "b.txt", roots),
          "nested not-yet-existing leaf accepted");

    // The root itself resolves to relative "." -- containment holds.
    check(aoc::core::isPathInsideAllowlist(root, roots),
          "root itself accepted");

    // Traversal escapes must be rejected.
    check(!aoc::core::isPathInsideAllowlist(root / ".." / "escape.csv", roots),
          "single .. traversal rejected");
    check(!aoc::core::isPathInsideAllowlist(
              root / ".." / ".." / "etc" / "passwd", roots),
          "deep .. traversal rejected");

    // Sibling directory sharing the root's name as a string prefix.
    const std::filesystem::path evilSibling = scratch / "dumps-evil";
    std::filesystem::create_directories(evilSibling);
    check(!aoc::core::isPathInsideAllowlist(evilSibling / "x.csv", roots),
          "prefix-confusable sibling rejected");

    // Completely unrelated absolute path.
    check(!aoc::core::isPathInsideAllowlist(
              std::filesystem::path("/etc/passwd"), roots),
          "unrelated absolute path rejected");

    // Empty allowlist rejects everything.
    check(!aoc::core::isPathInsideAllowlist(root / "a.csv", {}),
          "empty allowlist rejects");

    // Symlink inside the root pointing outside must be rejected once
    // canonicalised. Symlink creation can fail on exotic filesystems;
    // skip the case rather than fail spuriously.
    const std::filesystem::path outside = scratch / "outside";
    std::filesystem::create_directories(outside);
    const std::filesystem::path link = root / "sneaky";
    std::error_code linkEc;
    std::filesystem::create_directory_symlink(outside, link, linkEc);
    if (!linkEc) {
        check(!aoc::core::isPathInsideAllowlist(link / "c.csv", roots),
              "escaping symlink rejected");
    }

    std::error_code cleanupEc;
    std::filesystem::remove_all(scratch, cleanupEc);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_path_guard: %d check(s) failed\n",
                     g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_path_guard: all checks passed\n");
    return EXIT_SUCCESS;
}
