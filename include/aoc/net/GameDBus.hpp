/**
 * @file GameDBus.hpp
 * @brief Session-bus DBus service exposed by the game process.
 *
 * Allows external tools (MCP shims, test harnesses, debug scripts) to drive
 * the running game without X input synthesis. The service name is
 * org.aoc.Game, object /org/aoc/Game, interface org.aoc.Game.
 *
 * Current methods:
 *   TakeScreenshot(s path) -> (b ok, s message)
 *     Requests a screenshot of the game window to the given absolute path.
 *     The service records the request and the render loop performs it on
 *     the next frame boundary. Returns after the shot is written (the call
 *     blocks the caller but not the game loop).
 *
 * Linux/systemd only. On other platforms (or when libsystemd is missing at
 * configure time) this class compiles to a no-op shim so the rest of the
 * codebase can call it unconditionally.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace aoc::net {

class GameDBus {
public:
    GameDBus();
    ~GameDBus();

    GameDBus(const GameDBus&)            = delete;
    GameDBus& operator=(const GameDBus&) = delete;

    /// Register the service on the session bus. Safe to call once per
    /// process; subsequent calls are ignored. Returns false if registration
    /// failed (no bus, name taken, or built without libsystemd).
    bool start();

    /// Tear down the bus connection. Invoked by the destructor.
    void stop();

    /// Drain any pending DBus messages. Non-blocking. Must be called from
    /// the same thread that owns the bus (the render thread). Returns the
    /// number of messages processed this tick.
    int32_t tick();

    /// Pop the most recently requested screenshot path, if any. Returns
    /// an empty string when none is pending. The caller is expected to
    /// satisfy the request and then publish the outcome via
    /// reportScreenshotResult() so the DBus reply can be sent back.
    [[nodiscard]] std::string takePendingScreenshotPath();

    /// Record the outcome of the last screenshot request. `ok=true` marks
    /// success, `message` is a diagnostic returned to the caller.
    void reportScreenshotResult(bool ok, std::string message);

    /// True if a DBus client is currently blocked waiting on a screenshot
    /// reply. The render loop uses this to know whether to publish.
    [[nodiscard]] bool hasPendingReply() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    std::atomic<bool> m_active{false};
};

} // namespace aoc::net
