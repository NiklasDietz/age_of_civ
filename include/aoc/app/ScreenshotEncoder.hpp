/**
 * @file ScreenshotEncoder.hpp
 * @brief Write RGBA/BGRA pixel buffers to disk as PNG via stb_image_write.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::app {

/**
 * @brief Encode a pixel buffer and write it to disk as a PNG file.
 *
 * @param path          Absolute filesystem path (parent directory must exist).
 * @param pixels        Tight-packed 8-bit-per-channel pixel buffer,
 *                      4 bytes per pixel, row-major, top-down.
 * @param width         Image width in pixels.
 * @param height        Image height in pixels.
 * @param inputIsBgra   If true, the buffer is BGRA and will be swizzled to
 *                      RGBA before encoding. Set this when reading directly
 *                      from a BGRA Vulkan swapchain.
 * @return true on successful write; false if stb_image_write rejected the
 *         buffer (usually a bad path or out-of-space).
 */
bool writeScreenshotPng(const std::string& path,
                         const std::vector<uint8_t>& pixels,
                         uint32_t width,
                         uint32_t height,
                         bool inputIsBgra);

} // namespace aoc::app
