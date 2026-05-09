#pragma once

/**
 * @file DebugCommandFile.hpp
 * @brief Live debug hook via file watcher. Polls a command file
 *        each frame, executes any pending command, writes the
 *        result to an output file. Designed for an outside agent
 *        (script, LLM tool) to drive the running game without
 *        threading or networking.
 *
 * Wire format (text, line-based, one command per file):
 *   <verb> [arg1] [arg2] ...
 *
 * Round-trip:
 *   1. Caller writes /tmp/aoc_debug.cmd (single-line command).
 *   2. Game's next frame reads, dispatches, deletes /tmp/aoc_debug.cmd.
 *   3. Game writes /tmp/aoc_debug.out atomically (rename pattern).
 *   4. Caller reads /tmp/aoc_debug.out.
 *
 * The watcher is intentionally polled-on-each-frame in the main
 * thread -- no shared state, no locks. At 60 fps, command latency
 * is ~16 ms typical, ~30 ms p99 including filesystem flush. Good
 * enough for interactive debug; not for streaming telemetry.
 *
 * Commands supported (v1):
 *   info                       summary stats (seed, time, plate count, mtn count)
 *   dump-plates PATH           per-plate CSV from live HexGrid
 *   dump-grid PATH             ASCII hex map (terrain glyphs)
 *   set-creator-time MY        jump continent-creator scrub to MY (snaps to epoch)
 *   re-roll SEED               force creator re-roll with explicit seed
 *   quit                       clean shutdown
 */

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace aoc::app {

class DebugCommandFile {
public:
    /// Handler signature: receives args (everything after verb), returns
    /// the textual response to write to the output file. Throwing or
    /// returning empty string is allowed.
    using Handler = std::function<std::string(const std::string& args)>;

    /// Configure paths. Defaults match the docstring above.
    DebugCommandFile(std::filesystem::path cmdPath  = "/tmp/aoc_debug.cmd",
                     std::filesystem::path outPath  = "/tmp/aoc_debug.out");

    /// Register a verb handler. Replaces any prior handler for the same
    /// verb. Verbs are case-sensitive ASCII tokens.
    void registerHandler(std::string verb, Handler handler);

    /// Called once per frame. Reads the command file if present,
    /// dispatches to the registered handler, writes the output file,
    /// removes the command file. Safe to call when no command is
    /// pending; cheap (one stat() + one open() per frame).
    void poll();

private:
    std::filesystem::path m_cmdPath;
    std::filesystem::path m_outPath;
    std::unordered_map<std::string, Handler> m_handlers;
};

} // namespace aoc::app
