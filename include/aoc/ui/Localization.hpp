#pragma once

/**
 * @file Localization.hpp
 * @brief Lightweight string-key → display-text mapping (i18n).
 *
 * Wrap any user-facing literal with `tr("key")` instead of using the
 * raw English string. The catalog loader populates a singleton table
 * from a flat key=value file (locale/<lang>.txt). Missing keys fall
 * back to the key itself so partial translations stay readable.
 *
 * Format mirrors the existing settings.cfg loader (key=value lines)
 * to avoid pulling a JSON dependency just for translations.
 */

#include <string>
#include <string_view>
#include <unordered_map>

namespace aoc::ui {

class Localization {
public:
    /// Singleton accessor — single-threaded UI.
    static Localization& instance();

    /// Replace the current catalog. Called on locale change.
    void setCatalog(std::unordered_map<std::string, std::string> catalog) {
        this->m_catalog = std::move(catalog);
    }

    /// Lookup. Falls back to `key` itself when missing so untranslated
    /// keys render as the developer-facing identifier (better than
    /// blank text — surfaces the omission visually).
    [[nodiscard]] std::string_view lookup(std::string_view key) const;

    /// Load a catalog from a flat key=value text file. Lines beginning
    /// with `#` are comments; blank lines ignored. Returns true on
    /// successful load (even with zero entries).
    bool loadFromFile(const std::string& path);

private:
    std::unordered_map<std::string, std::string> m_catalog;
};

/// Convenience wrapper. Use this everywhere a label string would land.
[[nodiscard]] inline std::string_view tr(std::string_view key) {
    return Localization::instance().lookup(key);
}

} // namespace aoc::ui
