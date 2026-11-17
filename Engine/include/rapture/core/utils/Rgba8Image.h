#ifndef RAPTURE__RGBA8_IMAGE_H
#define RAPTURE__RGBA8_IMAGE_H

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Rapture {

/**
 * @brief An image in CPU memory, four 8 bit channels per pixel, rows top to bottom
 */
struct Rgba8Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

/**
 * @brief Copies a rectangle out of an image, clamped to the image
 * @param image The image to copy from
 * @param x Left edge of the rectangle in pixels
 * @param y Top edge of the rectangle in pixels
 * @param width Width of the rectangle in pixels
 * @param height Height of the rectangle in pixels
 * @return The copied rectangle, empty if it lies outside the image
 */
Rgba8Image Rgba8Image_crop(const Rgba8Image &image, int64_t x, int64_t y, int64_t width, int64_t height);

/**
 * @brief Writes an image to a PNG file
 * @param image The image to write
 * @param path The file to write
 * @return True if the file was written
 */
bool Rgba8Image_writePng(const Rgba8Image &image, const std::filesystem::path &path);

} // namespace Rapture

#endif // RAPTURE__RGBA8_IMAGE_H
