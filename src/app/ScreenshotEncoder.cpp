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
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

namespace aoc::app {

namespace {

/// stb write callback: forwards each chunk to a file descriptor we opened
/// ourselves (with O_NOFOLLOW). `context` is the fd boxed as an int*.
/// Records failure by zeroing the fd box so the caller can detect it.
void writeToFd(void* context, void* data, int size) {
    int* fd = static_cast<int*>(context);
    if (*fd < 0 || size <= 0) { return; }
    const char* buf = static_cast<const char*>(data);
    int remaining = size;
    while (remaining > 0) {
        ssize_t n = ::write(*fd, buf, static_cast<size_t>(remaining));
        if (n < 0) {
            if (errno == EINTR) { continue; }
            *fd = -1; // mark stream as failed; subsequent calls no-op.
            return;
        }
        buf += n;
        remaining -= static_cast<int>(n);
    }
}

} // namespace

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

    // Open the target ourselves with O_NOFOLLOW so a symlink planted at the
    // last path component (which weakly_canonical-based allowlisting in the
    // DBus layer cannot catch -- the leaf does not exist at check time) is
    // rejected here at write time with ELOOP, defeating the TOCTOU window.
    // O_CLOEXEC keeps the fd from leaking across exec; 0600 matches the
    // private-by-default policy for user data.
    int fd = ::open(path.c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                    0600);
    if (fd < 0) {
        return false;
    }

    const int stride = static_cast<int>(width) * 4;
    int writeFd = fd;
    const int result = stbi_write_png_to_func(&writeToFd, &writeFd,
                                              static_cast<int>(width),
                                              static_cast<int>(height),
                                              4, data, stride);
    const bool writeFailed = (writeFd < 0); // writeToFd zeroes box on error.
    const bool closeOk = (::close(fd) == 0);
    return result != 0 && !writeFailed && closeOk;
}

} // namespace aoc::app
