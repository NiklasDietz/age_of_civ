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

#include <cstdint>
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
        auto it = this->m_values.find(key);
        return (it != this->m_values.end()) ? it->second : defaultVal;
    }

    /// Get an integer value.
    [[nodiscard]] int32_t getInt(const std::string& key, int32_t defaultVal = 0) const {
        auto it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        return std::atoi(it->second.c_str());
    }

    /// Get a float value.
    [[nodiscard]] float getFloat(const std::string& key, float defaultVal = 0.0f) const {
        auto it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        return std::strtof(it->second.c_str(), nullptr);
    }

    /// Get a boolean value (true/false/yes/no/1/0).
    [[nodiscard]] bool getBool(const std::string& key, bool defaultVal = false) const {
        auto it = this->m_values.find(key);
        if (it == this->m_values.end()) { return defaultVal; }
        const std::string& val = it->second;
        return val == "true" || val == "yes" || val == "1";
    }

    /// Get a list value (parsed from [item1, item2, ...]).
    [[nodiscard]] std::vector<std::string> getList(const std::string& key) const {
        std::vector<std::string> result;
        auto it = this->m_values.find(key);
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
