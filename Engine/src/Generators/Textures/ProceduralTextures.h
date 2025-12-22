#ifndef RAPTURE__PROCEDURAL_TEXTURES_H
#define RAPTURE__PROCEDURAL_TEXTURES_H

#include "AssetManager/Asset.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include <cstring>
#include <memory>
#include <string>

namespace Rapture {

/**
 * @brief Configuration for creating procedural textures.
 *
 * Specifies the output texture format, filtering, and wrapping modes.
 * By default creates an RGBA8 texture suitable for most procedural content.
 */
struct ProceduralTextureConfig {
    TextureFormat format = TextureFormat::RGBA8;
    TextureFilter filter = TextureFilter::Linear;
    TextureWrap wrap = TextureWrap::Repeat;
    bool srgb = false;
    std::string name = ""; ///< Optional name for registering with AssetManager. Empty = auto-generated.
};

/**
 * @brief Push constant data for white noise generation.
 */
struct WhiteNoisePushConstants {
    uint32_t seed;
};

/**
 * @brief Generates textures using compute shaders.
 *
 * ProceduralTexture provides a flexible system for generating textures via compute shaders.
 * It supports any compute shader that writes to a storage image at set 4, binding 0.
 *
 * The system verifies that push constant struct sizes match the shader's expected size
 * at runtime. Textures are always 1024x1024 in the current implementation.
 *
 * @note Sets 0-3 are reserved by the engine. Custom shaders must use set 4, binding 0
 *       for the output storage image.
 *
 * Usage example:
 * @code
 * // Create a procedural texture generator
 * ProceduralTexture generator("glsl/Generators/PerlinNoise.cs.glsl");
 *
 * // Set push constants before generating
 * PerlinNoisePushConstants pc{.octaves = 4, .scale = 10.0f};
 * generator.setPushConstants(pc);
 *
 * // Generate the texture (records commands to the provided command buffer)
 * generator.generate(commandBuffer);
 *
 * // Get the resulting texture
 * auto texture = generator.getTexture();
 * @endcode
 *
 * For common procedural textures, use the static helper methods:
 * @code
 * auto noiseTexture = ProceduralTexture::generateWhiteNoise(commandBuffer, seed);
 * @endcode
 */
class ProceduralTexture {
  public:
    /**
     * @brief Creates a procedural texture generator from a shader path.
     *
     * @param shaderPath Path to the compute shader (.glsl or .spv), relative to shader directory.
     * @param config Optional texture configuration.
     */
    ProceduralTexture(const std::string &shaderPath, const ProceduralTextureConfig &config = ProceduralTextureConfig());

    /**
     * @brief Creates a procedural texture generator from an existing shader asset.
     *
     * @param shaderHandle Asset handle to a pre-loaded compute shader.
     * @param config Optional texture configuration.
     */
    ProceduralTexture(const AssetHandle &shaderHandle, const ProceduralTextureConfig &config = ProceduralTextureConfig());

    /**
     * @brief Creates a procedural texture generator with an existing output texture.
     *
     * Use this constructor when you want to regenerate into an existing texture,
     * such as for animated procedural textures.
     *
     * @param shaderPath Path to the compute shader (.glsl or .spv), relative to shader directory.
     * @param outputTexture Existing texture to write into. Must have storageImage = true.
     */
    ProceduralTexture(const std::string &shaderPath, std::shared_ptr<Texture> outputTexture);

    ~ProceduralTexture();

    /**
     * @brief Sets the push constant data for the shader.
     *
     * Call this before generate() to set shader parameters. The struct size
     * must match the shader's push_constant layout size.
     *
     * @tparam T Push constant struct type.
     * @param pushConstants Push constant values to use.
     * @return true if the size matches the shader's push constant layout, false otherwise.
     */
    template <typename T> bool setPushConstants(const T &pushConstants)
    {
        if (!verifyPushConstantSize(sizeof(T))) {
            return false;
        }
        m_pushConstantData.resize(sizeof(T));
        std::memcpy(m_pushConstantData.data(), &pushConstants, sizeof(T));
        return true;
    }

    /**
     * @brief Generates the texture.
     *
     * Records compute commands, submits to the GPU, and waits for completion.
     * The texture will be transitioned to GENERAL layout for writing, then to
     * SHADER_READ_ONLY_OPTIMAL after generation.
     *
     * This method is self-contained and can be called from any thread.
     * Only the calling thread will block while waiting for GPU completion.
     */
    void generate();

    /**
     * @brief Gets the generated texture.
     *
     * @return Shared pointer to the output texture. The texture is created during
     *         construction but only filled after generate() is called and the
     *         command buffer is submitted.
     */
    std::shared_ptr<Texture> getTexture() const { return m_texture; }

    /**
     * @brief Checks if the generator was initialized successfully.
     *
     * @return true if the shader was loaded and pipeline created successfully.
     */
    bool isValid() const { return m_isValid; }

    /**
     * @brief Gets the expected push constant size from the shader.
     *
     * @return Size in bytes of the shader's push constant block, or 0 if none.
     */
    size_t getExpectedPushConstantSize() const { return m_expectedPushConstantSize; }

    /**
     * @brief Generates a white noise texture.
     *
     * Static helper method that creates a one-shot white noise texture.
     * Uses the built-in WhiteNoise.cs.glsl shader. Handles command buffer
     * creation and submission internally.
     *
     * @param seed Random seed for noise generation.
     * @param config Optional texture configuration.
     * @return Shared pointer to the generated texture.
     */
    static std::shared_ptr<Texture> generateWhiteNoise(uint32_t seed = 0,
                                                       const ProceduralTextureConfig &config = ProceduralTextureConfig());

  private:
    void initFromShaderPath(const std::string &shaderPath, bool createTexture = true);
    void initFromShaderHandle(const AssetHandle &shaderHandle, bool createTexture = true);
    void initPipeline();
    void initDescriptorSet();
    void initTexture();
    void initCommandBuffer();
    void extractExpectedPushConstantSize();
    bool verifyPushConstantSize(size_t providedSize);

    std::shared_ptr<Shader> m_shader;
    std::shared_ptr<ComputePipeline> m_pipeline;
    std::shared_ptr<DescriptorSet> m_descriptorSet;
    std::shared_ptr<Texture> m_texture;
    std::shared_ptr<CommandPool> m_commandPool;
    std::shared_ptr<CommandBuffer> m_commandBuffer;

    std::vector<uint8_t> m_pushConstantData;
    size_t m_expectedPushConstantSize = 0;
    ProceduralTextureConfig m_config;
    bool m_isValid = false;

    static constexpr uint32_t TEXTURE_SIZE = 1024;
    static constexpr uint32_t WORKGROUP_SIZE = 8;
};

} // namespace Rapture

#endif // RAPTURE__PROCEDURAL_TEXTURES_H
