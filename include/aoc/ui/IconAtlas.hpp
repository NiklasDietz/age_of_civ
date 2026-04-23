#pragma once

/**
 * @file IconAtlas.hpp
 * @brief Sprite-atlas lookup for IconData / PortraitData / RichTextSpan.
 *
 * Until the real renderer path is wired the atlas holds only metadata
 * (region rects keyed by string name) and a fallback-colour table so
 * widgets can render a meaningful placeholder per icon. When the sprite
 * renderer lands, `lookupRegion` will return UV rects the renderer
 * samples from a single texture.
 *
 * String-keyed lookup so screens can do `atlas.id("resources.wheat")`
 * and get back a stable `uint32_t` spriteId to store in IconData.
 */

#include "aoc/ui/Widget.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aoc::ui {

struct IconRegion {
    /// UV rect in the atlas texture. Zero-sized when no real art loaded.
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    /// Fallback colour used by the current placeholder renderer.
    Color fallback = {0.5f, 0.5f, 0.5f, 1.0f};
};

class IconAtlas {
public:
    static IconAtlas& instance();

    /// Register a sprite. Returns stable id; repeat registrations of
    /// the same name return the same id.
    uint32_t registerSprite(std::string name, IconRegion region);

    /// Lookup by name. Returns 0 if unknown.
    [[nodiscard]] uint32_t id(std::string_view name) const;

    /// Lookup region by id. Returns nullptr if invalid.
    [[nodiscard]] const IconRegion* region(uint32_t id) const;

    /// Load atlas manifest from a key=value file (name=r,g,b,a).
    /// Returns entries loaded. Used as a placeholder art pipeline until
    /// real PNG atlases + loader land.
    int32_t loadPlaceholders(const std::string& path);

    /// Preload a built-in set of civ / resource / tech placeholder
    /// colours so screens that reference the canonical names
    /// (`resources.wheat`, `civs.rome`, etc.) have something to show
    /// even without a data file.
    void seedBuiltIns();

private:
    std::unordered_map<std::string, uint32_t> m_byName;
    std::vector<IconRegion>                   m_regions;
};

} // namespace aoc::ui
