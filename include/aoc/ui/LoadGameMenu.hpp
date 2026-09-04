#pragma once

/**
 * @file LoadGameMenu.hpp
 * @brief Main-menu save-slot picker: one Load button per slot plus Back.
 */

#include "aoc/save/SaveSlots.hpp"
#include "aoc/ui/IScreen.hpp"
#include "aoc/ui/Widget.hpp"

#include <array>
#include <functional>
#include <string>

namespace aoc::ui {

class UIManager;

class LoadGameMenu : public IScreen {
public:
    /// Numbered slots followed by the quicksave row (index aoc::save::QUICKSAVE_SLOT).
    static constexpr int ROW_COUNT = aoc::save::SAVE_SLOT_COUNT + 1;
    using SlotFlags                = std::array<bool, ROW_COUNT>;

    /// Empty slots (occupied[i] == false) render disabled with an "(empty)" label.
    void build(UIManager& ui, float screenW, float screenH, const SlotFlags& occupied,
               std::function<void(int)> onLoadSlot, std::function<void()> onBack);

    void destroy(UIManager& ui);

    /// Replace the status line under the slot list (load errors); survives resize.
    void setStatus(UIManager& ui, const std::string& text);

    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    [[nodiscard]] bool isOpen() const override { return this->m_isBuilt; }
    void close(UIManager& ui) override { this->destroy(ui); }
    void onResize(UIManager& ui, float width, float height) override;

private:
    bool m_isBuilt         = false;
    WidgetId m_rootPanel   = INVALID_WIDGET;
    WidgetId m_statusLabel = INVALID_WIDGET;
    SlotFlags m_occupied{};
    std::string m_status;

    std::function<void(int)> m_onLoadSlot;
    std::function<void()> m_onBack;
};

} // namespace aoc::ui
