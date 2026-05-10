/**
 * @file DebugCommandFile.cpp
 * @brief See DebugCommandFile.hpp for protocol + wire format.
 */

#include "aoc/app/DebugCommandFile.hpp"

#include "aoc/core/Log.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace aoc::app {

DebugCommandFile::DebugCommandFile(std::filesystem::path cmdPath,
                                   std::filesystem::path outPath)
    : m_cmdPath(std::move(cmdPath)), m_outPath(std::move(outPath)) {}

void DebugCommandFile::registerHandler(std::string verb, Handler handler) {
    this->m_handlers[std::move(verb)] = std::move(handler);
}

void DebugCommandFile::poll() {
    // `exists` failure (permission error, dead mount) is not the same
    // as "file is absent". Surface the failure with a single warning
    // then bail; spamming every poll would drown the log. We only
    // proceed when `exists` returns true AND `ec` is clear.
    std::error_code existsEc;
    const bool present = std::filesystem::exists(this->m_cmdPath, existsEc);
    if (existsEc) {
        LOG_WARN("DebugCommandFile: stat failed on %s: %s",
                 this->m_cmdPath.c_str(), existsEc.message().c_str());
        return;
    }
    if (!present) return;

    // Read whole file. Single line expected; tolerate trailing newline.
    std::ifstream in(this->m_cmdPath);
    if (!in.is_open()) return;
    std::string line;
    std::getline(in, line);
    in.close();
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }

    // Split verb + args.
    std::string verb;
    std::string args;
    {
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) {
            verb = line;
        } else {
            verb = line.substr(0, sp);
            args = line.substr(sp + 1);
        }
    }

    // Dispatch.
    std::string response;
    auto it = this->m_handlers.find(verb);
    if (it == this->m_handlers.end()) {
        std::ostringstream o;
        o << "{\"error\":\"unknown verb '" << verb
          << "'\",\"known\":[";
        bool first = true;
        for (const auto& kv : this->m_handlers) {
            if (!first) o << ',';
            first = false;
            o << '"' << kv.first << '"';
        }
        o << "]}";
        response = o.str();
    } else {
        try {
            response = it->second(args);
        } catch (const std::exception& e) {
            std::ostringstream o;
            o << "{\"error\":\"handler threw\",\"what\":\"" << e.what()
              << "\",\"verb\":\"" << verb << "\"}";
            response = o.str();
        }
    }

    // Atomic-replace pattern: write to temp, rename. Avoids the caller
    // reading a half-written response.
    const std::filesystem::path tmp = this->m_outPath.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << response;
        if (response.empty() || response.back() != '\n') out << '\n';
    }

    // Rename failure means the caller will read a stale response
    // file. We CANNOT safely re-dispatch: the handler may already
    // have mutated game state (set-creator-time, re-roll). Removing
    // the cmd file even on rename failure prevents a stuck cmd from
    // re-executing on every subsequent poll. The caller sees no fresh
    // response, which is strictly better than silent double-mutation.
    std::error_code renameEc;
    std::filesystem::rename(tmp, this->m_outPath, renameEc);
    if (renameEc) {
        LOG_WARN("DebugCommandFile: rename %s -> %s failed: %s "
                 "(removing cmd file anyway to avoid re-dispatch)",
                 tmp.c_str(),
                 this->m_outPath.c_str(),
                 renameEc.message().c_str());
    }

    // Remove cmd file last so caller knows the round-trip is done.
    // Failure here is non-fatal -- the caller already has a fresh
    // response file -- but if the cmd file is still present on the
    // next poll the verb runs again, which would silently re-execute
    // mutating handlers. Log so a stuck cmd file is visible.
    std::error_code removeEc;
    std::filesystem::remove(this->m_cmdPath, removeEc);
    if (removeEc) {
        LOG_WARN("DebugCommandFile: remove %s failed: %s",
                 this->m_cmdPath.c_str(),
                 removeEc.message().c_str());
    }
}

} // namespace aoc::app
