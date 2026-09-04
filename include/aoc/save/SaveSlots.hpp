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

/// Slot files live in the working directory; `slot` is 0-based, the file name 1-based.
[[nodiscard]] inline std::string saveSlotFilename(int slot) {
    return "save_slot_" + std::to_string(slot + 1) + ".aoc";
}

[[nodiscard]] inline bool saveSlotExists(int slot) {
    std::error_code ec;
    return std::filesystem::is_regular_file(saveSlotFilename(slot), ec);
}

} // namespace aoc::save
