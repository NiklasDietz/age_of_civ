#pragma once

/**
 * @file UIPersistence.hpp
 * @brief Save / restore per-screen UI state (scroll positions, tab
 *        indices, dragged-panel positions) so reloading a save file
 *        also restores the user's view.
 *
 * The persistence layer is opt-in: screens declare which keys they
 * want to round-trip via the `UiState` struct, and the host serializer
 * stores it next to the game save. Hot-reload (#20) reuses the same
 * data path with the source being a JSON-ish file rather than a
 * binary save section.
 */

#include "aoc/ui/Widget.hpp"

#include <string>
#include <unordered_map>

namespace aoc::ui {

/// Tagged union of value types we round-trip. Sized to fit comfortably
/// in 16 bytes; copies are cheap.
struct UiStateValue {
    enum class Kind : uint8_t { Int, Float, Bool, String, Rect };
    Kind kind = Kind::Int;
    int32_t i = 0;
    float f = 0.0f;
    bool  b = false;
    std::string s;
    Rect  r{};
};

/// Per-screen state map. Keyed by user-supplied identifier (e.g.
/// "city.activeTab" or "minimap.x"). Screens populate `state` from
/// their widgets just before save and consume it just after load.
struct UiState {
    std::unordered_map<std::string, UiStateValue> state;

    void setInt(const std::string& key, int32_t v) {
        UiStateValue u; u.kind = UiStateValue::Kind::Int; u.i = v;
        this->state[key] = std::move(u);
    }
    void setFloat(const std::string& key, float v) {
        UiStateValue u; u.kind = UiStateValue::Kind::Float; u.f = v;
        this->state[key] = std::move(u);
    }
    void setBool(const std::string& key, bool v) {
        UiStateValue u; u.kind = UiStateValue::Kind::Bool; u.b = v;
        this->state[key] = std::move(u);
    }
    void setString(const std::string& key, std::string v) {
        UiStateValue u; u.kind = UiStateValue::Kind::String; u.s = std::move(v);
        this->state[key] = std::move(u);
    }
    void setRect(const std::string& key, Rect v) {
        UiStateValue u; u.kind = UiStateValue::Kind::Rect; u.r = v;
        this->state[key] = std::move(u);
    }

    [[nodiscard]] int32_t getInt(const std::string& key, int32_t fallback = 0) const {
        auto it = this->state.find(key);
        return (it != this->state.end()) ? it->second.i : fallback;
    }
    [[nodiscard]] float getFloat(const std::string& key, float fallback = 0.0f) const {
        auto it = this->state.find(key);
        return (it != this->state.end()) ? it->second.f : fallback;
    }
    [[nodiscard]] bool getBool(const std::string& key, bool fallback = false) const {
        auto it = this->state.find(key);
        return (it != this->state.end()) ? it->second.b : fallback;
    }
    [[nodiscard]] const std::string& getString(const std::string& key) const {
        static const std::string empty;
        auto it = this->state.find(key);
        return (it != this->state.end()) ? it->second.s : empty;
    }
    [[nodiscard]] Rect getRect(const std::string& key, Rect fallback = {}) const {
        auto it = this->state.find(key);
        return (it != this->state.end()) ? it->second.r : fallback;
    }
};

/// Hot-reload: parse a layout description file and apply size/position
/// changes to existing root widgets. Format mirrors the i18n catalog
/// (`widgetTag.x=10` / `widgetTag.w=420`). Skips unknown tags so
/// partial layouts are safe. Returns the number of overrides applied.
int32_t hotReloadLayout(class UIManager& ui, const std::string& path);

} // namespace aoc::ui
