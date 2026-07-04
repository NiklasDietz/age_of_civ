/**
 * @file test_decision_log_io.cpp
 * @brief Pins write-error propagation in DecisionLog (aoc::core).
 *
 * DecisionLog's write path ignored every fwrite result: open() emitted the
 * header and returned true even if the write failed, and the log* methods and
 * close() swallowed errors, so a full disk produced a silently-truncated trace
 * that later failed to read with no indication of the cause. open() now flushes
 * and checks the header write, every record latches a sticky m_writeError, and
 * close() reports it; hasWriteError() lets callers surface a corrupt trace.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/DecisionLog.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

std::string tempTracePath() {
    return (std::filesystem::temp_directory_path() / "aoc_test_decision_log.trace").string();
}

} // namespace

TEST_CASE("DecisionLog round-trips a successful trace with no write error") {
    const std::string path = tempTracePath();
    std::remove(path.c_str());

    aoc::core::DecisionLog log;
    aoc::core::FileHeader hdr{};
    hdr.numPlayers = 2;
    REQUIRE(log.open(path, hdr));
    CHECK(log.active());

    log.logTurnSummary(1, 0, aoc::core::TurnSummary{});
    log.close();

    CHECK(log.hasWriteError() == false);
    CHECK(log.active() == false);
    std::remove(path.c_str());
}

#if defined(__linux__)
TEST_CASE("DecisionLog surfaces a write failure instead of silently succeeding") {
    // /dev/full opens fine but fails every write with ENOSPC -- a deterministic
    // Linux trigger. open() flushes the header, so the failure is caught and
    // reported there; pre-fix, open() ignored the fwrite result and returned true.
    // Skip if the environment lacks /dev/full (some minimal containers do): then
    // the failure trigger isn't available and the assertions wouldn't be meaningful.
    std::FILE* probe = std::fopen("/dev/full", "wb");
    if (probe == nullptr) {
        MESSAGE("skipping: /dev/full unavailable in this environment");
        return;
    }
    std::fclose(probe);

    aoc::core::DecisionLog log;
    aoc::core::FileHeader hdr{};
    hdr.numPlayers = 4;

    CHECK(log.open("/dev/full", hdr) == false);
    CHECK(log.hasWriteError() == true);
    CHECK(log.active() == false);  // a failed open leaves no live stream
}
#endif
