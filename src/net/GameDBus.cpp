/**
 * @file GameDBus.cpp
 * @brief sd-bus backed implementation of GameDBus.
 */

#include "aoc/net/GameDBus.hpp"
#include "aoc/core/Log.hpp"

#include <cstring>
#include <mutex>
#include <utility>

#ifdef AOC_HAS_SDBUS
#  include <systemd/sd-bus.h>
#endif

namespace aoc::net {

#ifdef AOC_HAS_SDBUS

struct GameDBus::Impl {
    sd_bus*         bus             = nullptr;
    sd_bus_slot*    vtableSlot      = nullptr;
    // Pending screenshot request, set by the DBus thread (= bus thread,
    // here identical to the render thread since tick() polls from it).
    std::mutex      pendingMutex;
    std::string     pendingPath;
    // Reply handling: when a client calls TakeScreenshot, sd-bus hands us
    // a `sd_bus_message*` that we must reply to. We stash the message and
    // send the reply once the render loop reports success.
    sd_bus_message* pendingCall     = nullptr;
    bool            resultOk        = false;
    std::string     resultMessage;
    bool            resultReady     = false;

    static int onTakeScreenshot(sd_bus_message* m, void* userdata, sd_bus_error* error) {
        Impl* self = static_cast<Impl*>(userdata);
        const char* path = nullptr;
        int readResult = sd_bus_message_read(m, "s", &path);
        if (readResult < 0 || path == nullptr || path[0] == '\0') {
            return sd_bus_error_set_const(error,
                "org.aoc.Game.Error.BadArgs",
                "path must be a non-empty string");
        }
        if (path[0] != '/') {
            return sd_bus_error_set_const(error,
                "org.aoc.Game.Error.BadArgs",
                "path must be absolute");
        }

        std::lock_guard<std::mutex> lock(self->pendingMutex);
        if (self->pendingCall != nullptr) {
            return sd_bus_error_set_const(error,
                "org.aoc.Game.Error.Busy",
                "a screenshot is already in flight");
        }
        self->pendingPath = path;
        self->pendingCall = sd_bus_message_ref(m);
        self->resultReady = false;
        // Returning 1 tells sd-bus that we will send the reply
        // asynchronously via sd_bus_reply_method_return on pendingCall.
        return 1;
    }
};

GameDBus::GameDBus()  = default;

GameDBus::~GameDBus() { this->stop(); }

bool GameDBus::start() {
    if (this->m_active.load()) { return true; }

    auto* impl = new Impl();
    int ret = sd_bus_open_user(&impl->bus);
    if (ret < 0) {
        LOG_WARN("GameDBus: sd_bus_open_user failed: %s", std::strerror(-ret));
        delete impl;
        return false;
    }

    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("TakeScreenshot", "s", "bs",
                      &Impl::onTakeScreenshot,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
    };

    ret = sd_bus_add_object_vtable(
        impl->bus,
        &impl->vtableSlot,
        "/org/aoc/Game",
        "org.aoc.Game",
        kVtable,
        impl);
    if (ret < 0) {
        LOG_WARN("GameDBus: sd_bus_add_object_vtable failed: %s", std::strerror(-ret));
        sd_bus_unref(impl->bus);
        delete impl;
        return false;
    }

    ret = sd_bus_request_name(impl->bus, "org.aoc.Game", 0);
    if (ret < 0) {
        LOG_WARN("GameDBus: could not acquire name org.aoc.Game: %s",
                 std::strerror(-ret));
        sd_bus_slot_unref(impl->vtableSlot);
        sd_bus_unref(impl->bus);
        delete impl;
        return false;
    }

    this->m_impl = impl;
    this->m_active.store(true);
    LOG_INFO("GameDBus: registered org.aoc.Game on session bus");
    return true;
}

void GameDBus::stop() {
    if (!this->m_active.load()) { return; }
    if (this->m_impl != nullptr) {
        if (this->m_impl->pendingCall != nullptr) {
            sd_bus_message_unref(this->m_impl->pendingCall);
            this->m_impl->pendingCall = nullptr;
        }
        if (this->m_impl->vtableSlot != nullptr) {
            sd_bus_slot_unref(this->m_impl->vtableSlot);
        }
        if (this->m_impl->bus != nullptr) {
            sd_bus_flush(this->m_impl->bus);
            sd_bus_unref(this->m_impl->bus);
        }
        delete this->m_impl;
        this->m_impl = nullptr;
    }
    this->m_active.store(false);
}

int32_t GameDBus::tick() {
    if (!this->m_active.load() || this->m_impl == nullptr) { return 0; }
    int32_t processed = 0;
    for (;;) {
        int r = sd_bus_process(this->m_impl->bus, nullptr);
        if (r <= 0) { break; }
        ++processed;
    }

    // Publish any ready reply. Doing this inside tick() keeps all sd-bus
    // writes on the same thread that owns the bus connection.
    Impl* impl = this->m_impl;
    std::unique_lock<std::mutex> lock(impl->pendingMutex);
    if (impl->pendingCall != nullptr && impl->resultReady) {
        sd_bus_message* msg = impl->pendingCall;
        impl->pendingCall = nullptr;
        const bool ok = impl->resultOk;
        std::string message = std::move(impl->resultMessage);
        impl->resultReady = false;
        lock.unlock();

        int r = sd_bus_reply_method_return(msg, "bs",
                                           static_cast<int>(ok),
                                           message.c_str());
        if (r < 0) {
            LOG_WARN("GameDBus: failed to send reply: %s", std::strerror(-r));
        }
        sd_bus_message_unref(msg);
    }
    return processed;
}

std::string GameDBus::takePendingScreenshotPath() {
    if (!this->m_active.load() || this->m_impl == nullptr) { return {}; }
    std::lock_guard<std::mutex> lock(this->m_impl->pendingMutex);
    if (this->m_impl->pendingPath.empty() || this->m_impl->resultReady) {
        return {};
    }
    return this->m_impl->pendingPath;
}

void GameDBus::reportScreenshotResult(bool ok, std::string message) {
    if (!this->m_active.load() || this->m_impl == nullptr) { return; }
    std::lock_guard<std::mutex> lock(this->m_impl->pendingMutex);
    this->m_impl->resultOk      = ok;
    this->m_impl->resultMessage = std::move(message);
    this->m_impl->resultReady   = true;
    this->m_impl->pendingPath.clear();
}

bool GameDBus::hasPendingReply() const {
    if (!this->m_active.load() || this->m_impl == nullptr) { return false; }
    std::lock_guard<std::mutex> lock(this->m_impl->pendingMutex);
    return this->m_impl->pendingCall != nullptr;
}

#else // AOC_HAS_SDBUS

struct GameDBus::Impl {};

GameDBus::GameDBus()  = default;
GameDBus::~GameDBus() = default;

bool GameDBus::start() { return false; }
void GameDBus::stop()  {}
int32_t GameDBus::tick() { return 0; }
std::string GameDBus::takePendingScreenshotPath() { return {}; }
void GameDBus::reportScreenshotResult(bool, std::string) {}
bool GameDBus::hasPendingReply() const { return false; }

#endif // AOC_HAS_SDBUS

} // namespace aoc::net
