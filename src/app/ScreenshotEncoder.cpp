/**
 * @file ScreenshotEncoder.cpp
 * @brief PNG encoder wrapper around stb_image_write; also hosts the stb
 *        implementation (STB_IMAGE_WRITE_IMPLEMENTATION) so exactly one TU
 *        expands it.
 */

#include "aoc/app/ScreenshotEncoder.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#include <algorithm>

namespace aoc::app {

bool writeScreenshotPng(const std::string& path,
                         const std::vector<uint8_t>& pixels,
                         uint32_t width,
                         uint32_t height,
                         bool inputIsBgra) {
    if (pixels.size() < static_cast<size_t>(width) * height * 4) {
        return false;
    }

    // stb writes RGBA. If the source is BGRA (typical swapchain layout),
    // swizzle into a scratch buffer so we do not mutate the caller's vector.
    std::vector<uint8_t> rgba;
    const uint8_t* data = pixels.data();
    if (inputIsBgra) {
        rgba.resize(pixels.size());
        const size_t pixelCount = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < pixelCount; ++i) {
            const size_t base = i * 4;
            rgba[base + 0] = pixels[base + 2];
            rgba[base + 1] = pixels[base + 1];
            rgba[base + 2] = pixels[base + 0];
            rgba[base + 3] = pixels[base + 3];
        }
        data = rgba.data();
    }

    const int stride = static_cast<int>(width) * 4;
    const int result = stbi_write_png(path.c_str(),
                                       static_cast<int>(width),
                                       static_cast<int>(height),
                                       4, data, stride);
    return result != 0;
}

} // namespace aoc::app
