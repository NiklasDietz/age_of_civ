/**
 * @file main.cpp
 * @brief Entry point for Age of Civilization.
 *
 * Supported command-line flags:
 *   --spectate            Start directly in spectator mode (all-AI game, renders live).
 *   --players <N>         Number of AI players for spectator mode (2-12, default 8).
 *   --turns <N>           Maximum turns for spectator mode (100-2000, default 500).
 */

#include "aoc/app/Application.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace {
// Shader + data paths in Renderer2D / Renderer3D / SpriteRenderer are hardcoded
// as "shaders/..." and "data/..." relative to the current working directory.
// CMake copies those trees next to the executable at build time, so make CWD
// match the executable location before Vulkan init runs.
void chdirToExecutableDirectory(const char* argv0) {
    std::error_code ec;
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        exePath = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (ec) { return; }
    }
    std::filesystem::current_path(exePath.parent_path(), ec);
}
} // namespace

int main(int argc, char* argv[]) {
    chdirToExecutableDirectory(argv[0]);

    bool spectateMode = false;
    int32_t spectatePlayers = 8;
    int32_t spectateTurns   = 500;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--spectate") {
            spectateMode = true;
        } else if (arg == "--players" && i + 1 < argc) {
            spectatePlayers = std::atoi(argv[++i]);
        } else if (arg == "--turns" && i + 1 < argc) {
            spectateTurns = std::atoi(argv[++i]);
        }
    }

    aoc::app::Application app;

    aoc::app::Application::Config config{};
    config.window.width  = 1280;
    config.window.height = 720;
    config.window.title  = spectateMode ? "Age of Civilization - Spectator" : "Age of Civilization";
    config.window.vsync  = true;

    // Validation layers require the Vulkan SDK to be installed.
    // Default to off -- enable only if explicitly requested.
    config.enableValidation = false;

    aoc::ErrorCode result = app.initialize(config);
    if (result != aoc::ErrorCode::Ok) {
        LOG_ERROR("Initialization failed (error %u): %.*s",
                  static_cast<unsigned>(result),
                  static_cast<int>(aoc::describeError(result).size()),
                  aoc::describeError(result).data());
        return EXIT_FAILURE;
    }

    if (spectateMode) {
        LOG_INFO("Starting spectator mode: %d players, %d turns",
                 spectatePlayers, spectateTurns);
        // Defer startSpectate until the first frame of run() so the render
        // pipeline is fully initialized (window visible, swapchain created).
        app.setDeferredSpectate(spectatePlayers, spectateTurns);
    }

    app.run();
    app.shutdown();

    return EXIT_SUCCESS;
}
