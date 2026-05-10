#pragma once

/**
 * @file DebugServer.hpp
 * @brief Localhost HTTP debug server for live game introspection.
 *
 * Bound strictly to 127.0.0.1 and to a port supplied by the caller
 * (default 9876). Runs `httplib::Server` on its own `std::jthread`;
 * routes are registered by the owner via `route()` after construction
 * and before `start()`. Routes execute on cpp-httplib worker threads,
 * so handlers MUST NOT mutate game state that lives on the main /
 * render thread.
 *
 * Concurrency contract for mutation routes:
 *   - The server provides no mutation queue, snapshot, or shared-state
 *     primitive. Each handler is responsible for parking the requested
 *     change in `std::atomic` flags owned by the handler's enclosing
 *     subsystem (see `aoc::app::Application::m_pendingCreatorTime`,
 *     `m_pendingReroll`, `m_quitRequested`).
 *   - The owning subsystem drains those flags at a known safe point
 *     on the main thread (top of the per-frame loop in `Application::run`).
 *   - Read-only routes (e.g. `/info`, `/plates`) tolerate the race --
 *     they observe a momentarily stale `HexGrid`. This is acceptable
 *     for diagnostic queries and explicitly NOT for mutation.
 *
 * Lifecycle:
 *   1. Construct with port (binds nothing yet).
 *   2. `route(method, path, handler)` for every endpoint.
 *   3. `start()` -- spawns the listener jthread.
 *   4. Server thread runs until `stop()` is called or the process
 *      exits. Destructor calls stop(); the jthread auto-joins.
 *
 * Threading:
 *   - Listener thread: blocks in cpp-httplib's accept loop.
 *   - Per-request: cpp-httplib uses a thread pool (size set by CMake
 *     via `CPPHTTPLIB_THREAD_POOL_COUNT`; default 8). Every handler
 *     call may run concurrently; handlers must be re-entrant.
 *
 * Localhost-only is a deliberate security choice: the API exposes
 * mutation surfaces that must never face the network.
 */

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace httplib { class Server; struct Request; struct Response; }

namespace aoc::debug {

class DebugServer {
public:
    using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

    /// Verb tag for `route()` registration.
    enum class Method { Get, Post };

    explicit DebugServer(int32_t port = 9876);
    ~DebugServer();

    DebugServer(const DebugServer&)            = delete;
    DebugServer& operator=(const DebugServer&) = delete;

    /// Register an HTTP handler. Must be called before `start()`.
    void route(Method method, std::string path, Handler handler);

    /// Convenience: register a handler that takes a query-parameter
    /// map and returns a JSON string. Status defaults to 200, content
    /// type defaults to `application/json`. Avoids requiring callers
    /// to include `httplib.h`.
    using JsonHandler = std::function<std::string(
        const std::unordered_map<std::string, std::string>& query,
        const std::string& body)>;
    void routeJson(Method method, std::string path, JsonHandler handler);

    /// Spawn the listener thread. Returns once the socket is bound;
    /// `false` indicates bind failure (port in use, perms, etc.).
    bool start();

    /// Stop accepting connections and join the listener thread.
    /// Idempotent.
    void stop();

    /// `true` between `start()` and `stop()` calls.
    [[nodiscard]] bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aoc::debug
