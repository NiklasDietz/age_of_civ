/**
 * @file DebugServer.cpp
 * @brief See DebugServer.hpp for protocol + threading guarantees.
 */

#include "aoc/debug/DebugServer.hpp"

#include "aoc/core/Log.hpp"

// CPPHTTPLIB_THREAD_POOL_COUNT is intentionally NOT defined here.
// Defining it inside this single TU created an ODR risk: any other TU
// that includes <cpp-httplib/httplib.h> with the default count would
// see a different `httplib::ThreadPool::DEFAULT_POOL_COUNT` and the
// inline functions in the header would emit conflicting definitions.
// The thread-pool size is now configured globally via CMake's
// `target_compile_definitions` (see workpackages 4 + 7); leave the
// default if no definition is supplied.
#include "cpp-httplib/httplib.h"

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace aoc::debug {

/// Escape `"`, `\`, and control characters for embedding inside a JSON
/// string literal. cpp-httplib hands us the `std::exception::what()`
/// C-string (and attacker-supplied paths) verbatim, so payloads could
/// contain quotes that would break the JSON envelope or, worse, allow
/// attacker-controlled keys/values into the response. Control characters
/// (0x00-0x1F) are illegal raw inside a JSON string, so we emit the
/// standard short escapes for `\n \r \t` and `\u00XX` for the rest --
/// otherwise a strict parser would reject the whole response. Public
/// (declared in the header): also used to escape reflected dump paths.
std::string escapeJsonString(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    static constexpr char kHex[] = "0123456789abcdef";
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
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

namespace {

/// DNS-rebinding defense: a browser lured to an attacker domain that
/// resolves to 127.0.0.1 still sends the ATTACKER's name in the Host
/// header. Only "127.0.0.1" and "localhost", optionally followed by
/// ":<digits>", may reach the loopback-bound listener.
[[nodiscard]] bool isAllowedHostHeader(std::string_view host) {
    const std::size_t colon = host.find(':');
    const std::string_view name = host.substr(0, colon);
    if (name != "127.0.0.1" && name != "localhost") { return false; }
    if (colon == std::string_view::npos) { return true; }
    const std::string_view port = host.substr(colon + 1);
    if (port.empty()) { return false; }
    for (const char c : port) {
        if (c < '0' || c > '9') { return false; }
    }
    return true;
}

} // namespace

struct DebugServer::Impl {
    int32_t            port = 9876;
    httplib::Server    server;
    /// `std::jthread` makes shutdown self-cleaning: the destructor
    /// calls `request_stop()` (no-op for us, we do not pass a
    /// `std::stop_token`) and then `join()`. We still call `stop()`
    /// on the httplib::Server BEFORE the thread is destroyed so the
    /// listener loop returns; `running.store(false)` happens earlier
    /// still so any in-flight handler can short-circuit on the flag.
    std::jthread       listener;
    std::atomic<bool>  running{false};
};

DebugServer::DebugServer(int32_t port) : m_impl(std::make_unique<Impl>()) {
    // Reject privileged (<1024) and out-of-range ports up front. A bad
    // value would otherwise surface only as an opaque bind failure later.
    if (port < 1024 || port > 65535) {
        LOG_ERROR("DebugServer: port %d out of range [1024, 65535]; "
                  "falling back to default %d", port, this->m_impl->port);
        return;
    }
    this->m_impl->port = port;
    // Registered on the server (not per-route) so the Host check runs
    // before dispatch for EVERY route, including ones added later.
    this->m_impl->server.set_pre_routing_handler(
        [](const httplib::Request& req, httplib::Response& res)
            -> httplib::Server::HandlerResponse {
            if (isAllowedHostHeader(req.get_header_value("Host"))) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            res.status = 403;
            res.set_content("{\"error\":\"forbidden host\"}\n",
                            "application/json");
            return httplib::Server::HandlerResponse::Handled;
        });
}

DebugServer::~DebugServer() {
    this->stop();
}

void DebugServer::routeJson(Method method, std::string path,
                            JsonHandler handler) {
    this->route(method, std::move(path),
        [h = std::move(handler)](const httplib::Request& req,
                                  httplib::Response& res) {
            std::unordered_map<std::string, std::string> q;
            q.reserve(req.params.size());
            for (const auto& kv : req.params) {
                q.emplace(kv.first, kv.second);
            }
            try {
                std::string body = h(q, req.body);
                if (body.empty() || body.back() != '\n') body += '\n';
                res.set_content(std::move(body), "application/json");
            } catch (const ServiceUnavailableError& e) {
                res.status = 503;
                std::string err = "{\"error\":\"";
                err += escapeJsonString(e.what());
                err += "\"}\n";
                res.set_content(err, "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                std::string err = "{\"error\":\"handler threw\",\"what\":\"";
                err += escapeJsonString(e.what());
                err += "\"}\n";
                res.set_content(err, "application/json");
            } catch (...) {
                res.status = 500;
                res.set_content("{\"error\":\"unknown exception\"}\n",
                                "application/json");
            }
        });
}

void DebugServer::route(Method method, std::string path, Handler handler) {
    if (this->m_impl->running.load(std::memory_order_acquire)) {
        LOG_WARN("DebugServer: ignoring route(%s) registered after start()",
                 path.c_str());
        return;
    }
    switch (method) {
        case Method::Get:
            this->m_impl->server.Get(path, std::move(handler));
            break;
        case Method::Post:
            this->m_impl->server.Post(path, std::move(handler));
            break;
    }
}

bool DebugServer::start() {
    if (this->m_impl->running.load(std::memory_order_acquire)) return true;
    constexpr const char* kHost = "127.0.0.1";
    if (!this->m_impl->server.bind_to_port(kHost, this->m_impl->port)) {
        LOG_WARN("DebugServer: failed to bind to %s:%d (port in use?)",
                 kHost, this->m_impl->port);
        return false;
    }
    this->m_impl->running.store(true, std::memory_order_release);
    this->m_impl->listener = std::jthread([this]() {
        // An exception escaping a jthread body calls std::terminate.
        // Guard it: log, then drop `running` so the server is observably
        // stopped instead of wedged with a dead listener but live flag.
        try {
            LOG_INFO("DebugServer: listening on http://127.0.0.1:%d",
                     this->m_impl->port);
            this->m_impl->server.listen_after_bind();
            LOG_INFO("DebugServer: listener exited");
        } catch (const std::exception& e) {
            LOG_ERROR("DebugServer: listener thread threw: %s", e.what());
            this->m_impl->running.store(false, std::memory_order_release);
        } catch (...) {
            LOG_ERROR("DebugServer: listener thread threw unknown exception");
            this->m_impl->running.store(false, std::memory_order_release);
        }
    });
    return true;
}

void DebugServer::stop() {
    if (!this->m_impl) return;
    if (!this->m_impl->running.load(std::memory_order_acquire)) return;
    // Drop the running flag FIRST so any concurrent handler that
    // reads `isRunning()` sees the shutdown before we tear down the
    // socket. Without this ordering, an in-flight handler might
    // still observe `running == true` while the listener thread has
    // already returned from `listen_after_bind()`.
    this->m_impl->running.store(false, std::memory_order_release);
    this->m_impl->server.stop();
    // `std::jthread` auto-joins on destruction or move-assignment;
    // explicit join is unnecessary but harmless. We rely on the
    // implicit join when the next start() reassigns the member or
    // when ~Impl runs.
}

bool DebugServer::isRunning() const noexcept {
    return this->m_impl
        && this->m_impl->running.load(std::memory_order_acquire);
}

} // namespace aoc::debug
