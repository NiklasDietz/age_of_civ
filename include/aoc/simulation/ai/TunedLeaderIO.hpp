#pragma once

/**
 * @file TunedLeaderIO.hpp
 * @brief Parse a per-leader tuned-personality file (the "Hard AI" name=value
 *        block emitted by `aoc_evolve --out-dir` / the checkpoint writer).
 *
 * Shared by `aoc_simulate --tuned-dir` and the `aoc_evolve` compare mode so the
 * interchange format has exactly one reader. Lives in aoc_lib (depends only on
 * LeaderBehavior) rather than in a tool TU so both binaries can link it.
 */

#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <string>

namespace aoc::sim {

/// Parse the "Hard AI" block of a tuned-leader file at `path` into `out`.
///
/// Format: one `name = value` per line inside the block delimited by a line
/// starting with "Hard AI" and the next line starting with "Medium AI".
/// Unknown keys and comment lines are ignored; missing fields keep their
/// LeaderBehavior defaults, so field reordering and partial files are tolerated.
///
/// Security: values are parsed with std::stof and each is accepted only if it
/// is finite — a NaN/Inf token (or an out-of-float-range literal) is rejected
/// and that field keeps its default. A hostile or corrupt file therefore can
/// never inject non-finite genes into the simulation.
///
/// @return false if the file cannot be opened or contains no usable values.
[[nodiscard]] bool parseTunedLeader(const std::string& path, LeaderBehavior& out);

} // namespace aoc::sim
