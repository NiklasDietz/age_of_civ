#pragma once

/**
 * @file DiplomacyScreen.hpp
 * @brief Modal diplomacy screen showing relations and diplomatic actions.
 *
 * Displays all known players with their relation scores, stances, war status,
 * and provides buttons for declaring war, proposing peace, and open borders.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::sim {
class DiplomacyManager;
}

namespace aoc::ui {

class DiplomacyScreen final : public ScreenBase {
public:
    void setContext(aoc::ecs::World* world, PlayerId humanPlayer,
                    aoc::sim::DiplomacyManager* diplomacy);

    void open(UIManager& ui) override;
    void close(UIManager& ui) override;
    void refresh(UIManager& ui) override;

private:
    aoc::ecs::World*             m_world     = nullptr;
    aoc::sim::DiplomacyManager*  m_diplomacy = nullptr;
    PlayerId                     m_player    = INVALID_PLAYER;
    WidgetId                     m_playerList = INVALID_WIDGET;
    float m_screenW = 1280.0f;
    float m_screenH = 720.0f;
};

} // namespace aoc::ui
