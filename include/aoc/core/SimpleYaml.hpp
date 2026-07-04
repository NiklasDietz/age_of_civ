#pragma once

/**
 * @file SimpleYaml.hpp
 * @brief Minimal YAML-like config parser (flat key: value only, no nesting).
 *
 * Supports:
 *   - key: value pairs (string, int, float, bool)
 *   - # comments
 *   - Blank lines ignored
 *   - Lists as key: [item1, item2, item3]
 *
 * Does NOT support: nested objects, multi-line strings, anchors/aliases.
 */

#include "aoc/core/Log.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace aoc {

class SimpleYaml {
public:
    /// Load from a file. Returns false if file cannot be opened.
    bool loadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }
        std::string line;
        while (std::getline(file, line)) {
            this->parseLine(line);
        }
        return true;
    }

    /// Load from a string.
    void loadFromString(const std::string& content) {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            this->parseLine(line);
        }
    }

    /// Get a string value (returns defaultVal if key not found).
    [[nodiscard]] std::string getString(const std::string& key,
                                         const std::string& defaultVal = "") const {
        std::unordered_map<std::string, std::string>::const_iterator it = this->m_values.find(key);
        return (it != this->m_values.end()) ? it->second : defaultVal;
    }

    /// Get an integer value. Returns defaultVal if the key is missing, or if
    /// the value fails to parse / overflows int32_t. atoi() silently returns 0
    /// on garbage and clamps without notice on overflow; strtol with endptr
    /// and errno catches both. Audited 2026-05-10 (security).
    [[nodiscard]] int32_t getInt(const std::string& key, int32_t defaultVal = 0) const {
        std::unordered_map<std::string, std::string>::const_iterator it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        const char* str = it->second.c_str();
        char* endPtr = nullptr;
        errno = 0;
        long parsed = std::strtol(str, &endPtr, 10);
        if (endPtr == str) {
            LOG_WARN("SimpleYaml::getInt: '%s' has non-numeric value '%s', using default %d",
                     key.c_str(), str, defaultVal);
            return defaultVal;
        }
        if (errno == ERANGE || parsed < INT32_MIN || parsed > INT32_MAX) {
            LOG_WARN("SimpleYaml::getInt: '%s' value '%s' overflows int32, using default %d",
                     key.c_str(), str, defaultVal);
            return defaultVal;
        }
        return static_cast<int32_t>(parsed);
    }

    /// Get a float value. Returns defaultVal if the key is missing, or if the
    /// value fails to parse / overflows. strtof with endptr and errno catches
    /// both garbage (silently 0) and out-of-range input, mirroring getInt.
    [[nodiscard]] float getFloat(const std::string& key, float defaultVal = 0.0f) const {
        std::unordered_map<std::string, std::string>::const_iterator it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        const char* str = it->second.c_str();
        char* endPtr = nullptr;
        errno = 0;
        float parsed = std::strtof(str, &endPtr);
        if (endPtr == str) {
            LOG_WARN("SimpleYaml::getFloat: '%s' has non-numeric value '%s', using default %f",
                     key.c_str(), str, static_cast<double>(defaultVal));
            return defaultVal;
        }
        if (errno == ERANGE) {
            LOG_WARN("SimpleYaml::getFloat: '%s' value '%s' overflows float, using default %f",
                     key.c_str(), str, static_cast<double>(defaultVal));
            return defaultVal;
        }
        return parsed;
    }

    /// Get a boolean value (true/false/yes/no/1/0).
    [[nodiscard]] bool getBool(const std::string& key, bool defaultVal = false) const {
        std::unordered_map<std::string, std::string>::const_iterator it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        const std::string& val = it->second;
        return val == "true" || val == "yes" || val == "1";
    }

    /// Get a list value (parsed from [item1, item2, ...]).
    [[nodiscard]] std::vector<std::string> getList(const std::string& key) const {
        std::vector<std::string> result;
        std::unordered_map<std::string, std::string>::const_iterator it = this->m_values.find(key);
        if (it == this->m_values.end()) { return result; }

        std::string val = it->second;
        // Strip brackets
        if (!val.empty() && val.front() == '[') { val.erase(val.begin()); }
        if (!val.empty() && val.back() == ']')  { val.pop_back(); }

        std::istringstream ss(val);
        std::string item;
        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            std::size_t start = item.find_first_not_of(" \t");
            std::size_t end = item.find_last_not_of(" \t");
            if (start != std::string::npos) {
                result.push_back(item.substr(start, end - start + 1));
            }
        }
        return result;
    }

    /// Check if a key exists.
    [[nodiscard]] bool hasKey(const std::string& key) const {
        return this->m_values.count(key) > 0;
    }

private:
    void parseLine(const std::string& line) {
        // Skip empty lines and comments
        std::size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) { return; }
        if (line[firstNonSpace] == '#') { return; }

        // Find the colon separator
        std::size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) { return; }

        // Extract key (trim whitespace)
        std::string key = line.substr(0, colonPos);
        std::size_t keyStart = key.find_first_not_of(" \t");
        std::size_t keyEnd = key.find_last_not_of(" \t");
        if (keyStart == std::string::npos) { return; }
        key = key.substr(keyStart, keyEnd - keyStart + 1);

        // Extract value (trim whitespace, strip inline comments)
        std::string value = line.substr(colonPos + 1);
        std::size_t valStart = value.find_first_not_of(" \t");
        if (valStart == std::string::npos) {
            value = "";
        } else {
            value = value.substr(valStart);
            // Strip inline comment (but not inside brackets)
            bool inBrackets = false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '[') { inBrackets = true; }
                if (value[i] == ']') { inBrackets = false; }
                if (value[i] == '#' && !inBrackets) {
                    value = value.substr(0, i);
                    break;
                }
            }
            // Trim trailing whitespace
            std::size_t valEnd = value.find_last_not_of(" \t\r\n");
            if (valEnd != std::string::npos) {
                value = value.substr(0, valEnd + 1);
            }
        }

        this->m_values[key] = value;
    }

    std::unordered_map<std::string, std::string> m_values;
};

} // namespace aoc
