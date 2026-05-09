/**
 * @file DebugCommandFile.cpp
 * @brief See DebugCommandFile.hpp for protocol + wire format.
 */

#include "aoc/app/DebugCommandFile.hpp"

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
    std::error_code ec;
    if (!std::filesystem::exists(this->m_cmdPath, ec)) return;

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
    std::filesystem::rename(tmp, this->m_outPath, ec);

    // Remove cmd file last so caller knows the round-trip is done.
    std::filesystem::remove(this->m_cmdPath, ec);
}

} // namespace aoc::app
