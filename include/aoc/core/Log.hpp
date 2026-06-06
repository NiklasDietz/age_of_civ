#pragma once

/**
 * @file Log.hpp
 * @brief Structured logging with consistent severity levels.
 *
 * Provides LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL macros that
 * automatically include timestamp, severity, source file, and line number.
 * DEBUG is disabled in NDEBUG (release) builds.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace aoc::log {

enum class Severity : uint8_t {
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

/// Runtime minimum severity. Messages strictly below this are dropped
/// before any formatting or I/O — cheap enough to leave always-on.
/// Default: Debug (everything). GA harness bumps this to Warn to skip
/// the per-turn INFO spam that otherwise dominates wall time.
inline std::atomic<Severity> g_minSeverity{Severity::Debug};

inline void setMinSeverity(Severity s) noexcept {
    g_minSeverity.store(s, std::memory_order_relaxed);
}

[[nodiscard]] inline Severity minSeverity() noexcept {
    return g_minSeverity.load(std::memory_order_relaxed);
}

[[nodiscard]] inline bool shouldLog(Severity s) noexcept {
    return static_cast<uint8_t>(s) >=
           static_cast<uint8_t>(g_minSeverity.load(std::memory_order_relaxed));
}

[[nodiscard]] constexpr const char* severityTag(Severity severity) {
    switch (severity) {
        case Severity::Debug: return "DEBUG";
        case Severity::Info:  return "INFO ";
        case Severity::Warn:  return "WARN ";
        case Severity::Error: return "ERROR";
        case Severity::Fatal: return "FATAL";
        default:              return "?????";
    }
}

/// Core log function. Prefer using the macros below.
/// The fmt parameter is always a string literal when called via the LOG_* macros;
/// the -Wformat-nonliteral warning is suppressed because the template indirection
/// hides the literal from the compiler's format checker.
///
/// Atomicity: the previous implementation issued THREE separate
/// `std::fprintf` calls (header, body, newline). When two threads
/// log concurrently, the C runtime interleaves their writes and
/// produces unreadable garbled lines. We now format the entire line
/// into a stack buffer and emit it with a single `std::fprintf`,
/// which the C runtime guarantees is atomic against other stdio
/// calls on the same FILE* (POSIX `flockfile`/`funlockfile` per
/// stream). The 1024-byte cap is the line ceiling; longer messages
/// are truncated and a trailing `...` indicator is appended so the
/// truncation is visible.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
template<typename... Args>
void logMessage(Severity severity, const char* file, int line,
                const char* fmt, Args... args) {
    if (severity != Severity::Fatal && !shouldLog(severity)) { return; }
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    char timeBuf[20];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

    constexpr std::size_t kLogBufSize = 1024;
    char buf[kLogBufSize];
    int prefixLen = std::snprintf(buf, kLogBufSize, "[%s][%s] %s:%d ",
                                   timeBuf, severityTag(severity), file, line);
    if (prefixLen < 0) prefixLen = 0;
    if (static_cast<std::size_t>(prefixLen) >= kLogBufSize) {
        // Prefix already filled the buffer; nothing to append.
        prefixLen = static_cast<int>(kLogBufSize - 1);
    }
    int bodyLen = std::snprintf(buf + prefixLen, kLogBufSize - static_cast<std::size_t>(prefixLen),
                                 fmt, args...);
    if (bodyLen < 0) bodyLen = 0;
    std::size_t total = static_cast<std::size_t>(prefixLen)
                      + static_cast<std::size_t>(bodyLen);
    if (total >= kLogBufSize) {
        // Mark truncation and reserve room for "...\n\0".
        constexpr std::size_t kTruncTail = 5; // "...\n" + NUL.
        if (kLogBufSize > kTruncTail) {
            std::snprintf(buf + (kLogBufSize - kTruncTail), kTruncTail,
                          "...\n");
        }
        total = kLogBufSize - 1;
    } else {
        // Room for newline + NUL guaranteed (`total < kLogBufSize`).
        buf[total] = '\n';
        ++total;
        buf[total] = '\0';
    }

    std::FILE* stream = (severity >= Severity::Error) ? stderr : stdout;
    // Single fprintf: stdio guarantees per-call locking on the FILE*,
    // so no other thread's logMessage can interleave with this line.
    std::fprintf(stream, "%s", buf);

    if (severity == Severity::Fatal) {
        std::fflush(stderr);
        std::abort();
    }
}
#pragma GCC diagnostic pop

/// Overload for zero variadic args (format string only, no printf args).
inline void logMessage(Severity severity, const char* file, int line,
                       const char* msg) {
    if (severity != Severity::Fatal && !shouldLog(severity)) { return; }
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    char timeBuf[20];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

    constexpr std::size_t kLogBufSize = 1024;
    char buf[kLogBufSize];
    int written = std::snprintf(buf, kLogBufSize, "[%s][%s] %s:%d %s\n",
                                 timeBuf, severityTag(severity), file, line, msg);
    if (written < 0) { written = 0; }
    if (static_cast<std::size_t>(written) >= kLogBufSize) {
        // Mark truncation, mirroring the variadic overload: reserve room for
        // "...\n" + NUL in the last bytes of the buffer.
        constexpr std::size_t kTruncTail = 5; // "...\n" + NUL.
        if (kLogBufSize > kTruncTail) {
            std::snprintf(buf + (kLogBufSize - kTruncTail), kTruncTail,
                          "...\n");
        }
    }

    std::FILE* stream = (severity >= Severity::Error) ? stderr : stdout;
    std::fprintf(stream, "%s", buf);

    if (severity == Severity::Fatal) {
        std::fflush(stderr);
        std::abort();
    }
}

} // namespace aoc::log

// clang-format off
#ifdef NDEBUG
    #define LOG_DEBUG(fmt, ...) ((void)0)
#else
    #define LOG_DEBUG(fmt, ...) aoc::log::logMessage(aoc::log::Severity::Debug, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#endif

#define LOG_INFO(fmt, ...)  aoc::log::logMessage(aoc::log::Severity::Info,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(fmt, ...)  aoc::log::logMessage(aoc::log::Severity::Warn,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(fmt, ...) aoc::log::logMessage(aoc::log::Severity::Error, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(fmt, ...) aoc::log::logMessage(aoc::log::Severity::Fatal, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
// clang-format on
