/**
 * @file main.cpp
 * @brief Entry point for Age of Civilization.
 */

#include "aoc/app/Application.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Log.hpp"

#include <cstdlib>

int main() {
    aoc::app::Application app;

    aoc::app::Application::Config config{};
    config.window.width  = 1280;
    config.window.height = 720;
    config.window.title  = "Age of Civilization";
    config.window.vsync  = true;

#ifdef NDEBUG
    config.enableValidation = false;
#else
    config.enableValidation = true;
#endif

    aoc::ErrorCode result = app.initialize(config);
    if (result != aoc::ErrorCode::Ok) {
        LOG_ERROR("Initialization failed (error %u): %.*s",
                  static_cast<unsigned>(result),
                  static_cast<int>(aoc::describeError(result).size()),
                  aoc::describeError(result).data());
        return EXIT_FAILURE;
    }

    app.run();
    app.shutdown();

    return EXIT_SUCCESS;
}
