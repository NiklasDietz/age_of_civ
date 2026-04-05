#pragma once

/**
 * @file Log.hpp
 * @brief Structured logging with consistent severity levels.
 *
 * Provides LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL macros that
 * automatically include timestamp, severity, source file, and line number.
 * DEBUG is disabled in NDEBUG (release) builds.
 */

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
template<typename... Args>
void logMessage(Severity severity, const char* file, int line,
                const char* fmt, Args... args) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    char timeBuf[20];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

    std::FILE* stream = (severity >= Severity::Error) ? stderr : stdout;
    std::fprintf(stream, "[%s][%s] %s:%d ", timeBuf, severityTag(severity), file, line);
    std::fprintf(stream, fmt, args...);
    std::fprintf(stream, "\n");

    if (severity == Severity::Fatal) {
        std::fflush(stderr);
        std::abort();
    }
}
#pragma GCC diagnostic pop

/// Overload for zero variadic args (format string only, no printf args).
inline void logMessage(Severity severity, const char* file, int line,
                       const char* msg) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);

    char timeBuf[20];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

    std::FILE* stream = (severity >= Severity::Error) ? stderr : stdout;
    std::fprintf(stream, "[%s][%s] %s:%d %s\n", timeBuf, severityTag(severity), file, line, msg);

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
