#pragma once

/**
 * @file Encyclopedia.hpp
 * @brief In-game wiki / encyclopedia system.
 *
 * Provides a searchable, categorized reference for all game content:
 * units, buildings, improvements, resources, goods, production recipes,
 * technologies, governments, policies, wonders, and game mechanics.
 *
 * All entries are generated from the existing static definition tables
 * so they automatically stay in sync with gameplay data.
 *
 * Categories:
 *   - Units:         All unit types with stats, class, era, costs
 *   - Buildings:     All buildings with yields, district, costs
 *   - Improvements:  Tile improvements with yields, tech requirements
 *   - Resources:     Raw resources and where they appear
 *   - Goods:         All tradeable goods with prices, categories
 *   - Recipes:       Production chain recipes with inputs/outputs
 *   - Technologies:  Tech tree with prerequisites, unlocks
 *   - Governments:   Government types with bonuses, slots, corruption
 *   - Policies:      All policy cards with effects
 *   - Wonders:       World wonders with effects, prerequisites
 *   - Civilizations: All civs with unique abilities
 *   - Mechanics:     Game system explanations (monetary, trade, combat, etc.)
 */

#include "aoc/ui/GameScreens.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::ui { class UIManager; }

namespace aoc::ui {

// ============================================================================
// Encyclopedia entry
// ============================================================================

enum class WikiCategory : uint8_t {
    Units,
    Buildings,
    Improvements,
    Resources,
    Goods,
    Recipes,
    Technologies,
    Governments,
    Policies,
    Wonders,
    Civilizations,
    Mechanics,

    Count
};

[[nodiscard]] const char* wikiCategoryName(WikiCategory cat);

/// A single encyclopedia entry.
struct WikiEntry {
    WikiCategory category;
    std::string  title;       ///< Display name (searchable)
    std::string  body;        ///< Full description text (multi-line)
    std::string  statsBlock;  ///< Formatted stats (key: value pairs)
};

// ============================================================================
// Encyclopedia data builder
// ============================================================================

/// Build all encyclopedia entries from static game data.
/// Call once at game start or when data changes.
[[nodiscard]] std::vector<WikiEntry> buildEncyclopedia();

// ============================================================================
// Encyclopedia screen (UI)
// ============================================================================

class EncyclopediaScreen final : public ScreenBase {
public:
    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

    /// Navigate to a specific category.
    void setCategory(WikiCategory cat);

    /// Search entries by title substring.
    void search(const std::string& query);

private:
    void rebuildEntryList(UIManager& ui);

    std::vector<WikiEntry> m_allEntries;
    std::vector<const WikiEntry*> m_filteredEntries;  ///< Current view (after category/search filter)
    WikiCategory m_currentCategory = WikiCategory::Mechanics;
    std::string m_searchQuery;

    // Widget IDs
    WidgetId m_categoryPanel = INVALID_WIDGET;
    WidgetId m_entryList = INVALID_WIDGET;
    WidgetId m_detailLabel = INVALID_WIDGET;
    int32_t m_selectedEntry = -1;
};

} // namespace aoc::ui
