/**
 * @file GameDBus.cpp
 * @brief sd-bus backed implementation of GameDBus.
 */

#include "aoc/net/GameDBus.hpp"
#include "aoc/core/Log.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef AOC_HAS_SDBUS
#  include <systemd/sd-bus.h>
#endif

namespace aoc::net {

#ifdef AOC_HAS_SDBUS

namespace {

namespace fs = std::filesystem;

/// Build the allowlist of directories under which TakeScreenshot will
/// accept a target path. Order matters only for diagnostic logging --
/// any directory in the list authorises the prefix. The list is built
/// once per call so $XDG_DATA_HOME / $HOME changes are picked up.
[[nodiscard]] std::vector<fs::path> screenshotAllowlist() {
    std::vector<fs::path> roots;

    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
        std::error_code ec;
        fs::path candidate = fs::weakly_canonical(fs::path(xdg) / "aoc" / "screenshots", ec);
        if (!ec) { roots.push_back(std::move(candidate)); }
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        std::error_code ec;
        fs::path candidate = fs::weakly_canonical(fs::path(home) / "Pictures" / "aoc", ec);
        if (!ec) { roots.push_back(std::move(candidate)); }
        // Fallback default: $HOME/.local/share/aoc/screenshots when XDG_DATA_HOME unset.
        std::error_code ec2;
        fs::path defaultXdg = fs::weakly_canonical(
            fs::path(home) / ".local" / "share" / "aoc" / "screenshots", ec2);
        if (!ec2) { roots.push_back(std::move(defaultXdg)); }
    }

    return roots;
}

/// True iff `candidate` lies inside any allowlisted root after symlink
/// resolution. Uses `weakly_canonical` so the file does not need to
/// exist yet (TakeScreenshot writes to a fresh path). Equality of the
/// canonical roots is by lexical-prefix comparison on path components.
[[nodiscard]] bool isPathInsideAllowlist(const fs::path& candidate,
                                         const std::vector<fs::path>& roots) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(candidate, ec);
    if (ec) { return false; }
    for (const fs::path& root : roots) {
        // Compare component-by-component: candidate must start with the
        // full root prefix. lexically_relative returns a path beginning
        // with ".." when candidate is outside `root`.
        fs::path rel = canonical.lexically_relative(root);
        if (rel.empty()) { continue; }
        if (rel.native().rfind("..", 0) == 0) { continue; }
        return true;
    }
    return false;
}

} // namespace

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

    Impl() = default;

    /// Releases the sd-bus resources owned by this PIMPL. The order
    /// matches the inverse of acquisition in `GameDBus::start`:
    /// drop pending reply message → release vtable slot → flush and
    /// release the bus connection. Holding the destructor here lets
    /// the owner use `std::unique_ptr<Impl>` with the default deleter.
    ~Impl() {
        // pendingMutex is not held here: by the time Impl is destroyed
        // the public `stop()` has cleared all aliasing state and the
        // DBus thread is no longer running.
        if (pendingCall != nullptr) {
            sd_bus_message_unref(pendingCall);
            pendingCall = nullptr;
        }
        if (vtableSlot != nullptr) {
            sd_bus_slot_unref(vtableSlot);
            vtableSlot = nullptr;
        }
        if (bus != nullptr) {
            sd_bus_flush(bus);
            sd_bus_unref(bus);
            bus = nullptr;
        }
    }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

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

        // Reject any path that does not resolve under the screenshot
        // allowlist. `weakly_canonical` follows symlinks on every
        // existing component so the prefix check is TOCTOU-resistant
        // for parent directories. The leaf file is the one we are
        // about to write -- it does not exist yet, so a last-component
        // symlink cannot be caught here. That gap is closed at the
        // write site (ScreenshotEncoder.cpp), which opens the target
        // with O_NOFOLLOW and fails on a leaf symlink at write time.
        const std::vector<fs::path> roots = screenshotAllowlist();
        if (roots.empty()) {
            return sd_bus_error_set_const(error,
                "org.aoc.Game.Error.PolicyDenied",
                "no screenshot directory configured (set $XDG_DATA_HOME or $HOME)");
        }
        if (!isPathInsideAllowlist(fs::path(path), roots)) {
            return sd_bus_error_set_const(error,
                "org.aoc.Game.Error.PolicyDenied",
                "path is outside the screenshot allowlist "
                "($XDG_DATA_HOME/aoc/screenshots or $HOME/Pictures/aoc)");
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

    auto impl = std::make_unique<Impl>();
    int ret = sd_bus_open_user(&impl->bus);
    if (ret < 0) {
        LOG_WARN("GameDBus: sd_bus_open_user failed: %s", std::strerror(-ret));
        // unique_ptr destructor frees Impl + any partial state.
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
        impl.get());
    if (ret < 0) {
        LOG_WARN("GameDBus: sd_bus_add_object_vtable failed: %s", std::strerror(-ret));
        return false;
    }

    ret = sd_bus_request_name(impl->bus, "org.aoc.Game", 0);
    if (ret < 0) {
        LOG_WARN("GameDBus: could not acquire name org.aoc.Game: %s",
                 std::strerror(-ret));
        return false;
    }

    // Ordering invariant: the non-atomic write to `m_impl` is sequenced-
    // before the atomic store to `m_active` (seq_cst). All readers
    // (`tick`, `takePendingScreenshotPath`, `reportScreenshotResult`,
    // `hasPendingReply`) load `m_active` first; a true result establishes
    // happens-before with this publish, so `m_impl` is safe to read.
    // Never relax `m_active.store` below `release` -- doing so breaks
    // this publish and turns subsequent `m_impl` reads into a data race.
    this->m_impl = std::move(impl);
    this->m_active.store(true);
    LOG_INFO("GameDBus: registered org.aoc.Game on session bus");
    return true;
}

void GameDBus::stop() {
    if (!this->m_active.load()) { return; }
    if (this->m_impl != nullptr) {
        // Acquire pendingMutex before reading/writing pendingCall: a
        // racing DBus thread (vtable callback `onTakeScreenshot`) may
        // still be holding the mutex when stop() runs from a concurrent
        // shutdown path. Once the lock is released and `m_active` flips
        // to false, no new callbacks observe the publish.
        {
            std::lock_guard<std::mutex> lock(this->m_impl->pendingMutex);
            if (this->m_impl->pendingCall != nullptr) {
                // Reply with an error so the blocked client unblocks instead
                // of hanging forever waiting for a return that never comes.
                int r = sd_bus_reply_method_errorf(
                    this->m_impl->pendingCall,
                    "org.aoc.Game.Error.ShuttingDown",
                    "server shutting down");
                if (r < 0) {
                    LOG_WARN("GameDBus: failed to send shutdown error reply: %s",
                             std::strerror(-r));
                }
                sd_bus_message_unref(this->m_impl->pendingCall);
                this->m_impl->pendingCall = nullptr;
            }
        }
        // Impl's destructor handles slot/bus teardown.
        this->m_impl.reset();
    }
    this->m_active.store(false);
}

int32_t GameDBus::tick() {
    if (!this->m_active.load() || this->m_impl == nullptr) { return 0; }
    int32_t processed = 0;
    for (;;) {
        int r = sd_bus_process(this->m_impl->bus, nullptr);
        if (r < 0) {
            LOG_WARN("sd_bus_process error: %s", std::strerror(-r));
            break;
        }
        if (r == 0) { break; }
        ++processed;
    }

    // Publish any ready reply. Doing this inside tick() keeps all sd-bus
    // writes on the same thread that owns the bus connection.
    Impl* impl = this->m_impl.get();
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
