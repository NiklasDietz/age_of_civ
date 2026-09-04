#pragma once

/**
 * @file SaveSlots.hpp
 * @brief Numbered save-slot naming shared by every load/save UI path.
 */

#include <filesystem>
#include <string>
#include <system_error>

namespace aoc::save {

inline constexpr int SAVE_SLOT_COUNT = 5;

/// Single quick-save file written by the QuickSave hotkey and the return-to-menu Save.
inline constexpr const char* QUICKSAVE_FILENAME = "quicksave.aoc";

/// Pseudo-slot index of the quicksave, so load pickers can list it after the
/// numbered slots; it is load-only in the UI (F5 and the exit save write it).
inline constexpr int QUICKSAVE_SLOT = SAVE_SLOT_COUNT;

/// Slot files live in the working directory; `slot` is 0-based, the file name 1-based.
/// `QUICKSAVE_SLOT` maps to the quicksave file.
[[nodiscard]] inline std::string saveSlotFilename(int slot) {
    if (slot == QUICKSAVE_SLOT) {
        return QUICKSAVE_FILENAME;
    }
    return "save_slot_" + std::to_string(slot + 1) + ".aoc";
}

[[nodiscard]] inline bool saveSlotExists(int slot) {
    std::error_code ec;
    return std::filesystem::is_regular_file(saveSlotFilename(slot), ec);
}

} // namespace aoc::save
