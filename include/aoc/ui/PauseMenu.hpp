#pragma once

/**
 * @file PauseMenu.hpp
 * @brief In-game pause menu: Resume / Save / Load / Main Menu / Quit.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/ui/IScreen.hpp"

#include <functional>

namespace aoc::ui {

class UIManager;

class PauseMenu : public IScreen {
public:
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void()> onResume,
               std::function<void()> onSave,
               std::function<void()> onLoad,
               std::function<void()> onMainMenu,
               std::function<void()> onQuit);

    void destroy(UIManager& ui);

    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    [[nodiscard]] bool isOpen() const override { return this->m_isBuilt; }
    void close(UIManager& ui) override { this->destroy(ui); }
    void onResize(UIManager& ui, float width, float height) override;

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;
    float m_lastW = 0.0f;
    float m_lastH = 0.0f;

    std::function<void()> m_onResume;
    std::function<void()> m_onSave;
    std::function<void()> m_onLoad;
    std::function<void()> m_onMainMenu;
    std::function<void()> m_onQuit;
};

} // namespace aoc::ui
