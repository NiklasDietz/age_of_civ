#pragma once

/**
 * @file JsonParser.hpp
 * @brief Minimal JSON parser for loading game data files.
 *
 * Supports: objects, arrays, strings, numbers (int and float), booleans, null.
 * Not a general-purpose parser -- designed for the specific JSON structures
 * used by the game's data files. Does not handle escape sequences beyond
 * the basics (\\, \", \n, \t, \/).
 */

#include "aoc/core/Log.hpp"

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace aoc::data {

/// A JSON value: null, bool, int64, double, string, array, or object.
class JsonValue {
public:
    struct Null {};

    using Array  = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    using Value  = std::variant<Null, bool, int64_t, double, std::string, Array, Object>;

    JsonValue() : storage(Null{}) {}
    explicit JsonValue(bool v) : storage(v) {}
    explicit JsonValue(int64_t v) : storage(v) {}
    explicit JsonValue(double v) : storage(v) {}
    explicit JsonValue(std::string v) : storage(std::move(v)) {}
    explicit JsonValue(Array v) : storage(std::move(v)) {}
    explicit JsonValue(Object v) : storage(std::move(v)) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<Null>(this->storage); }
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(this->storage); }
    [[nodiscard]] bool isInt() const { return std::holds_alternative<int64_t>(this->storage); }
    [[nodiscard]] bool isDouble() const { return std::holds_alternative<double>(this->storage); }
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(this->storage); }
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(this->storage); }
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(this->storage); }

    /// Returns true if the value is a number (int or double).
    [[nodiscard]] bool isNumber() const { return this->isInt() || this->isDouble(); }

    [[nodiscard]] bool asBool() const { return std::get<bool>(this->storage); }
    [[nodiscard]] int64_t asInt() const { return std::get<int64_t>(this->storage); }
    [[nodiscard]] double asDouble() const {
        if (this->isInt()) { return static_cast<double>(std::get<int64_t>(this->storage)); }
        return std::get<double>(this->storage);
    }
    [[nodiscard]] float asFloat() const { return static_cast<float>(this->asDouble()); }
    [[nodiscard]] int32_t asInt32() const { return static_cast<int32_t>(this->asInt()); }
    [[nodiscard]] uint16_t asUint16() const { return static_cast<uint16_t>(this->asInt()); }
    [[nodiscard]] uint8_t asUint8() const { return static_cast<uint8_t>(this->asInt()); }
    [[nodiscard]] const std::string& asString() const { return std::get<std::string>(this->storage); }
    [[nodiscard]] const Array& asArray() const { return std::get<Array>(this->storage); }
    [[nodiscard]] const Object& asObject() const { return std::get<Object>(this->storage); }

    /// Access an object field by key. Returns a null JsonValue if the key is missing.
    [[nodiscard]] const JsonValue& operator[](const std::string& key) const {
        static const JsonValue NULL_VALUE;
        if (!this->isObject()) { return NULL_VALUE; }
        const Object& obj = std::get<Object>(this->storage);
        std::map<std::string, JsonValue>::const_iterator it = obj.find(key);
        if (it == obj.end()) { return NULL_VALUE; }
        return it->second;
    }

    /// Access an array element by index. Returns a null JsonValue if out of bounds.
    [[nodiscard]] const JsonValue& operator[](std::size_t index) const {
        static const JsonValue NULL_VALUE;
        if (!this->isArray()) { return NULL_VALUE; }
        const Array& arr = std::get<Array>(this->storage);
        if (index >= arr.size()) { return NULL_VALUE; }
        return arr[index];
    }

    /// Returns the number of elements (array) or fields (object). 0 for others.
    [[nodiscard]] std::size_t size() const {
        if (this->isArray()) { return std::get<Array>(this->storage).size(); }
        if (this->isObject()) { return std::get<Object>(this->storage).size(); }
        return 0;
    }

    /// Check if an object contains a key.
    [[nodiscard]] bool hasKey(const std::string& key) const {
        if (!this->isObject()) { return false; }
        const Object& obj = std::get<Object>(this->storage);
        return obj.find(key) != obj.end();
    }

private:
    Value storage;
};

/// Parse a JSON string into a JsonValue. Returns a null value on parse errors
/// and logs the error with file/line context.
[[nodiscard]] inline JsonValue parseJson(const std::string& input, const std::string& sourceFile = "<unknown>") {
    struct Parser {
        const std::string& text;
        std::size_t pos = 0;
        const std::string& fileName;

        void skipWhitespace() {
            while (this->pos < this->text.size()) {
                char c = this->text[this->pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    ++this->pos;
                } else {
                    break;
                }
            }
        }

        bool expect(char c) {
            this->skipWhitespace();
            if (this->pos < this->text.size() && this->text[this->pos] == c) {
                ++this->pos;
                return true;
            }
            return false;
        }

        char peek() {
            this->skipWhitespace();
            if (this->pos >= this->text.size()) { return '\0'; }
            return this->text[this->pos];
        }

        JsonValue parseValue() {
            this->skipWhitespace();
            if (this->pos >= this->text.size()) {
                LOG_ERROR("JSON parse error in '%s': unexpected end of input at position %zu",
                          this->fileName.c_str(), this->pos);
                return JsonValue{};
            }

            char c = this->text[this->pos];
            if (c == '"') { return this->parseString(); }
            if (c == '{') { return this->parseObject(); }
            if (c == '[') { return this->parseArray(); }
            if (c == 't' || c == 'f') { return this->parseBool(); }
            if (c == 'n') { return this->parseNull(); }
            if (c == '-' || (c >= '0' && c <= '9')) { return this->parseNumber(); }

            LOG_ERROR("JSON parse error in '%s': unexpected character '%c' at position %zu",
                      this->fileName.c_str(), c, this->pos);
            return JsonValue{};
        }

        JsonValue parseString() {
            ++this->pos;  // skip opening quote
            std::string result;
            while (this->pos < this->text.size()) {
                char c = this->text[this->pos];
                if (c == '"') {
                    ++this->pos;
                    return JsonValue{std::move(result)};
                }
                if (c == '\\') {
                    ++this->pos;
                    if (this->pos >= this->text.size()) { break; }
                    char escaped = this->text[this->pos];
                    switch (escaped) {
                        case '"':  result += '"'; break;
                        case '\\': result += '\\'; break;
                        case '/':  result += '/'; break;
                        case 'n':  result += '\n'; break;
                        case 't':  result += '\t'; break;
                        case 'r':  result += '\r'; break;
                        default:   result += escaped; break;
                    }
                } else {
                    result += c;
                }
                ++this->pos;
            }
            LOG_ERROR("JSON parse error in '%s': unterminated string at position %zu",
                      this->fileName.c_str(), this->pos);
            return JsonValue{std::move(result)};
        }

        JsonValue parseNumber() {
            std::size_t start = this->pos;
            bool isFloat = false;

            if (this->text[this->pos] == '-') { ++this->pos; }
            while (this->pos < this->text.size() && this->text[this->pos] >= '0' && this->text[this->pos] <= '9') {
                ++this->pos;
            }
            if (this->pos < this->text.size() && this->text[this->pos] == '.') {
                isFloat = true;
                ++this->pos;
                while (this->pos < this->text.size() && this->text[this->pos] >= '0' && this->text[this->pos] <= '9') {
                    ++this->pos;
                }
            }
            if (this->pos < this->text.size() && (this->text[this->pos] == 'e' || this->text[this->pos] == 'E')) {
                isFloat = true;
                ++this->pos;
                if (this->pos < this->text.size() && (this->text[this->pos] == '+' || this->text[this->pos] == '-')) {
                    ++this->pos;
                }
                while (this->pos < this->text.size() && this->text[this->pos] >= '0' && this->text[this->pos] <= '9') {
                    ++this->pos;
                }
            }

            std::string numStr = this->text.substr(start, this->pos - start);
            if (isFloat) {
                return JsonValue{std::strtod(numStr.c_str(), nullptr)};
            }
            return JsonValue{static_cast<int64_t>(std::strtoll(numStr.c_str(), nullptr, 10))};
        }

        JsonValue parseBool() {
            if (this->text.compare(this->pos, 4, "true") == 0) {
                this->pos += 4;
                return JsonValue{true};
            }
            if (this->text.compare(this->pos, 5, "false") == 0) {
                this->pos += 5;
                return JsonValue{false};
            }
            LOG_ERROR("JSON parse error in '%s': expected 'true' or 'false' at position %zu",
                      this->fileName.c_str(), this->pos);
            return JsonValue{};
        }

        JsonValue parseNull() {
            if (this->text.compare(this->pos, 4, "null") == 0) {
                this->pos += 4;
                return JsonValue{};
            }
            LOG_ERROR("JSON parse error in '%s': expected 'null' at position %zu",
                      this->fileName.c_str(), this->pos);
            return JsonValue{};
        }

        JsonValue parseArray() {
            ++this->pos;  // skip [
            JsonValue::Array arr;
            this->skipWhitespace();
            if (this->pos < this->text.size() && this->text[this->pos] == ']') {
                ++this->pos;
                return JsonValue{std::move(arr)};
            }
            while (true) {
                arr.push_back(this->parseValue());
                this->skipWhitespace();
                if (this->expect(',')) { continue; }
                if (this->expect(']')) { break; }
                LOG_ERROR("JSON parse error in '%s': expected ',' or ']' at position %zu",
                          this->fileName.c_str(), this->pos);
                break;
            }
            return JsonValue{std::move(arr)};
        }

        JsonValue parseObject() {
            ++this->pos;  // skip {
            JsonValue::Object obj;
            this->skipWhitespace();
            if (this->pos < this->text.size() && this->text[this->pos] == '}') {
                ++this->pos;
                return JsonValue{std::move(obj)};
            }
            while (true) {
                this->skipWhitespace();
                if (this->pos >= this->text.size() || this->text[this->pos] != '"') {
                    LOG_ERROR("JSON parse error in '%s': expected string key at position %zu",
                              this->fileName.c_str(), this->pos);
                    break;
                }
                JsonValue keyVal = this->parseString();
                std::string key = keyVal.asString();
                this->skipWhitespace();
                if (!this->expect(':')) {
                    LOG_ERROR("JSON parse error in '%s': expected ':' after key '%s' at position %zu",
                              this->fileName.c_str(), key.c_str(), this->pos);
                    break;
                }
                obj[std::move(key)] = this->parseValue();
                this->skipWhitespace();
                if (this->expect(',')) { continue; }
                if (this->expect('}')) { break; }
                LOG_ERROR("JSON parse error in '%s': expected ',' or '}' at position %zu",
                          this->fileName.c_str(), this->pos);
                break;
            }
            return JsonValue{std::move(obj)};
        }
    };

    Parser parser{input, 0, sourceFile};
    return parser.parseValue();
}

} // namespace aoc::data
