/**
 * @file HotkeyManager.cpp
 * @brief Rebindable hotkey system implementation.
 */

#include "aoc/app/HotkeyManager.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>
#include <sstream>

// GLFW key constants (subset, full list in GLFW headers)
namespace keys {
    constexpr int32_t KEY_T = 84;
    constexpr int32_t KEY_G = 71;
    constexpr int32_t KEY_E = 69;
    constexpr int32_t KEY_P = 80;
    constexpr int32_t KEY_W = 87;
    constexpr int32_t KEY_D = 68;
    constexpr int32_t KEY_R = 82;
    constexpr int32_t KEY_ESCAPE = 256;
    constexpr int32_t KEY_SPACE = 32;
    constexpr int32_t KEY_ENTER = 257;
    constexpr int32_t KEY_F = 70;
    constexpr int32_t KEY_S = 83;
    constexpr int32_t KEY_X = 88;
    constexpr int32_t KEY_A = 65;
    constexpr int32_t KEY_I = 73;
    constexpr int32_t KEY_L = 76;
    constexpr int32_t KEY_M = 77;
    constexpr int32_t KEY_1 = 49;
}

namespace aoc::app {

const char* actionName(GameAction action) {
    switch (action) {
        case GameAction::OpenTechScreen:        return "Tech Screen";
        case GameAction::OpenGovernmentScreen:   return "Government Screen";
        case GameAction::OpenEconomyScreen:      return "Economy Screen";
        case GameAction::OpenProductionScreen:   return "Production Screen";
        case GameAction::OpenEncyclopedia:       return "Encyclopedia";
        case GameAction::OpenDiplomacy:          return "Diplomacy";
        case GameAction::OpenReligion:           return "Religion";
        case GameAction::CloseScreen:            return "Close Screen";
        case GameAction::EndTurn:                return "End Turn";
        case GameAction::Confirm:                return "Confirm";
        case GameAction::Fortify:                return "Fortify";
        case GameAction::Sleep:                  return "Sleep";
        case GameAction::DeleteUnit:             return "Delete Unit";
        case GameAction::AutoExplore:            return "Auto-Explore";
        case GameAction::AutoImprove:            return "Auto-Improve";
        case GameAction::Quicksave:              return "Quicksave";
        case GameAction::Quickload:              return "Quickload";
        case GameAction::ToggleMinimap:          return "Toggle Minimap";
        default: {
            int32_t idx = static_cast<int32_t>(action) - static_cast<int32_t>(GameAction::ControlGroup1);
            if (idx >= 0 && idx < 9) { return "Control Group"; }
            return "Unknown";
        }
    }
}

void HotkeyManager::loadDefaults() {
    this->m_bindings[static_cast<int32_t>(GameAction::OpenTechScreen)]      = {keys::KEY_T};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenGovernmentScreen)]= {keys::KEY_G};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenEconomyScreen)]   = {keys::KEY_E};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenProductionScreen)]= {keys::KEY_P};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenEncyclopedia)]    = {keys::KEY_W};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenDiplomacy)]       = {keys::KEY_D};
    this->m_bindings[static_cast<int32_t>(GameAction::OpenReligion)]        = {keys::KEY_R};
    this->m_bindings[static_cast<int32_t>(GameAction::CloseScreen)]         = {keys::KEY_ESCAPE};
    this->m_bindings[static_cast<int32_t>(GameAction::EndTurn)]             = {keys::KEY_SPACE};
    this->m_bindings[static_cast<int32_t>(GameAction::Confirm)]             = {keys::KEY_ENTER};
    this->m_bindings[static_cast<int32_t>(GameAction::Fortify)]             = {keys::KEY_F};
    this->m_bindings[static_cast<int32_t>(GameAction::Sleep)]               = {keys::KEY_S};
    this->m_bindings[static_cast<int32_t>(GameAction::DeleteUnit)]          = {keys::KEY_X};
    this->m_bindings[static_cast<int32_t>(GameAction::AutoExplore)]         = {keys::KEY_A};
    this->m_bindings[static_cast<int32_t>(GameAction::AutoImprove)]         = {keys::KEY_I};
    this->m_bindings[static_cast<int32_t>(GameAction::Quicksave)]           = {keys::KEY_S, true};
    this->m_bindings[static_cast<int32_t>(GameAction::Quickload)]           = {keys::KEY_L, true};
    this->m_bindings[static_cast<int32_t>(GameAction::ToggleMinimap)]       = {keys::KEY_M};

    // Control groups 1-9
    for (int32_t i = 0; i < 9; ++i) {
        this->m_bindings[static_cast<int32_t>(GameAction::ControlGroup1) + i] = {keys::KEY_1 + i};
    }
}

bool HotkeyManager::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        this->loadDefaults();
        return false;
    }

    this->loadDefaults();  // Start with defaults, override from file

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') { continue; }

        std::istringstream iss(line);
        std::string actionStr;
        int32_t keyCode = 0;
        int32_t ctrl = 0;
        int32_t shift = 0;
        int32_t alt = 0;

        if (iss >> actionStr >> keyCode >> ctrl >> shift >> alt) {
            for (int32_t i = 0; i < ACTION_COUNT; ++i) {
                if (actionStr == actionName(static_cast<GameAction>(i))) {
                    this->m_bindings[i] = {keyCode, ctrl != 0, shift != 0, alt != 0};
                    break;
                }
            }
        }
    }

    LOG_INFO("Loaded hotkeys from %s", filepath.c_str());
    return true;
}

bool HotkeyManager::saveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << "# Age of Civilization Hotkey Configuration\n";
    file << "# Format: ActionName KeyCode Ctrl Shift Alt\n\n";

    for (int32_t i = 0; i < ACTION_COUNT; ++i) {
        const HotkeyBinding& b = this->m_bindings[i];
        file << actionName(static_cast<GameAction>(i)) << " "
             << b.keyCode << " "
             << (b.ctrl ? 1 : 0) << " "
             << (b.shift ? 1 : 0) << " "
             << (b.alt ? 1 : 0) << "\n";
    }

    return true;
}

void HotkeyManager::rebind(GameAction action, HotkeyBinding binding) {
    this->m_bindings[static_cast<int32_t>(action)] = binding;
}

const HotkeyBinding& HotkeyManager::getBinding(GameAction action) const {
    return this->m_bindings[static_cast<int32_t>(action)];
}

GameAction HotkeyManager::matchKey(int32_t keyCode, bool ctrl, bool shift, bool alt) const {
    for (int32_t i = 0; i < ACTION_COUNT; ++i) {
        const HotkeyBinding& b = this->m_bindings[i];
        if (b.keyCode == keyCode && b.ctrl == ctrl && b.shift == shift && b.alt == alt) {
            return static_cast<GameAction>(i);
        }
    }
    return GameAction::Count;
}

} // namespace aoc::app
