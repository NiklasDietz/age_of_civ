#pragma once

/**
 * @file CityListScreen.hpp
 * @brief Every own city on one screen: population against housing, the
 *        production item with its turns, loyalty and the amenity tier, with a
 *        Go button that centres the camera and an Open button that opens the
 *        City Detail screen. Civ VI plan Phase 2.3, 2026-09-05.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::ui {

class CityListScreen final : public ScreenBase {
public:
    using LocationCallback = std::function<void(aoc::hex::AxialCoord)>;

    /// Pointers are non-owning and must outlive the open screen.
    void setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                    PlayerId humanPlayer);
    /// `onJump` centres the camera on a city, `onOpen` opens its detail screen.
    void setCallbacks(LocationCallback onJump, LocationCallback onOpen);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    void buildRows(UIManager& ui);
    [[nodiscard]] uint64_t stateFingerprint() const;

    aoc::game::GameState* m_gameState = nullptr;
    const aoc::map::HexGrid* m_grid   = nullptr;
    PlayerId m_player                 = INVALID_PLAYER;
    LocationCallback m_onJump;
    LocationCallback m_onOpen;
    WidgetId m_summaryLabel           = INVALID_WIDGET;
    WidgetId m_list                   = INVALID_WIDGET;
    uint64_t m_shownFingerprint       = 0;
};

} // namespace aoc::ui
