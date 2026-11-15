#include "windowless_application_environment.h"

#include <rapture/gpu/textures/Texture.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

static constexpr uint32_t TEXTURE_SIZE = 64;
static constexpr std::array<uint8_t, 4> UPLOADED_COLOR = {10, 200, 30, 255};
static constexpr std::chrono::seconds UPLOAD_TIMEOUT = std::chrono::seconds(10);

class AsyncTextureUpload : public WindowlessGpuTest {
  protected:
    /**
     * @brief Uploads a texture filled with one color through the async path and reads every mip back
     * @param mipLevels The mip count of the texture, 0 for a full chain
     * @return The read back bytes, empty if the upload did not finish in time
     */
    static std::vector<uint8_t> uploadAndReadBack(uint32_t mipLevels)
    {
        Rapture::TextureSpecification spec;
        spec.width = TEXTURE_SIZE;
        spec.height = TEXTURE_SIZE;
        spec.format = Rapture::TextureFormat::RGBA8;
        spec.srgb = false;
        spec.mipLevels = mipLevels;
        spec.allowReadback = true;

        std::unique_ptr<Rapture::Texture> texture = Rapture::Texture::createPlaceholder(spec);

        std::vector<uint8_t> pixels;
        pixels.reserve(TEXTURE_SIZE * TEXTURE_SIZE * UPLOADED_COLOR.size());
        for (uint32_t pixel = 0; pixel < TEXTURE_SIZE * TEXTURE_SIZE; pixel++) {
            pixels.insert(pixels.end(), UPLOADED_COLOR.begin(), UPLOADED_COLOR.end());
        }

        texture->uploadDataAsync(std::move(pixels));

        auto deadline = std::chrono::steady_clock::now() + UPLOAD_TIMEOUT;
        while (!texture->isReady() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!texture->isReady()) {
            return {};
        }

        return texture->readbackData();
    }

    /**
     * @brief Whether every pixel of the read back bytes is the uploaded color
     * @param bytes The read back bytes
     */
    static bool isEveryPixelTheUploadedColor(const std::vector<uint8_t> &bytes)
    {
        if (bytes.empty() || bytes.size() % UPLOADED_COLOR.size() != 0) {
            return false;
        }
        for (size_t offset = 0; offset < bytes.size(); offset += UPLOADED_COLOR.size()) {
            for (size_t channel = 0; channel < UPLOADED_COLOR.size(); channel++) {
                if (bytes[offset + channel] != UPLOADED_COLOR[channel]) {
                    return false;
                }
            }
        }
        return true;
    }
};

TEST_F(AsyncTextureUpload, SingleMipArrivesIntact)
{
    std::vector<uint8_t> bytes = uploadAndReadBack(1);

    ASSERT_FALSE(bytes.empty()) << "the upload did not finish";
    EXPECT_EQ(bytes.size(), TEXTURE_SIZE * TEXTURE_SIZE * UPLOADED_COLOR.size());
    EXPECT_TRUE(isEveryPixelTheUploadedColor(bytes));
}

TEST_F(AsyncTextureUpload, MipChainIsGeneratedFromTheUpload)
{
    std::vector<uint8_t> bytes = uploadAndReadBack(0);

    ASSERT_FALSE(bytes.empty()) << "the upload did not finish";
    EXPECT_GT(bytes.size(), TEXTURE_SIZE * TEXTURE_SIZE * UPLOADED_COLOR.size());
    EXPECT_TRUE(isEveryPixelTheUploadedColor(bytes));
}
