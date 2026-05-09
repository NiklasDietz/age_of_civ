/**
 * @file DebugServer.cpp
 * @brief See DebugServer.hpp for protocol + threading guarantees.
 */

#include "aoc/debug/DebugServer.hpp"

#include "aoc/core/Log.hpp"

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "cpp-httplib/httplib.h"

#include <atomic>
#include <thread>
#include <utility>

namespace aoc::debug {

struct DebugServer::Impl {
    int32_t          port = 9876;
    httplib::Server  server;
    std::thread      listener;
    std::atomic<bool> running{false};
};

DebugServer::DebugServer(int32_t port) : m_impl(std::make_unique<Impl>()) {
    this->m_impl->port = port;
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
            } catch (const std::exception& e) {
                res.status = 500;
                std::string err = "{\"error\":\"handler threw\",\"what\":\"";
                err += e.what();
                err += "\"}\n";
                res.set_content(err, "application/json");
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
    this->m_impl->listener = std::thread([this]() {
        LOG_INFO("DebugServer: listening on http://127.0.0.1:%d",
                 this->m_impl->port);
        this->m_impl->server.listen_after_bind();
        LOG_INFO("DebugServer: listener exited");
    });
    return true;
}

void DebugServer::stop() {
    if (!this->m_impl) return;
    if (!this->m_impl->running.load(std::memory_order_acquire)) return;
    this->m_impl->server.stop();
    if (this->m_impl->listener.joinable()) {
        this->m_impl->listener.join();
    }
    this->m_impl->running.store(false, std::memory_order_release);
}

bool DebugServer::isRunning() const noexcept {
    return this->m_impl
        && this->m_impl->running.load(std::memory_order_acquire);
}

} // namespace aoc::debug
