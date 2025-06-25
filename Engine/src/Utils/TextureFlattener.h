#pragma once

#include <memory>
#include <string>

#include "Textures/Texture.h"
#include "Shaders/Shader.h"
#include "Pipelines/ComputePipeline.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"

namespace Rapture {

/**
 * @brief Represents a flattened texture with its associated data
 */
class FlattenTexture {
public:
    FlattenTexture(std::shared_ptr<Texture> inputTexture, std::shared_ptr<Texture> flattenedTexture, const std::string& name);
    ~FlattenTexture() = default;

    /**
     * @brief Update the flattened texture with new data from the input texture
     * @param commandBuffer Command buffer to record commands into
     */
    void update(std::shared_ptr<CommandBuffer> commandBuffer);

    /**
     * @brief Get the flattened texture
     */
    std::shared_ptr<Texture> getFlattenedTexture() const { return m_flattenedTexture; }

    /**
     * @brief Get the input texture
     */
    std::shared_ptr<Texture> getInputTexture() const { return m_inputTexture; }

    /**
     * @brief Get the name
     */
    const std::string& getName() const { return m_name; }

private:
    std::shared_ptr<Texture> m_inputTexture;
    std::shared_ptr<Texture> m_flattenedTexture;
    uint32_t m_inputTextureBindlessIndex = 0;
    std::string m_name;
};

/**
 * @brief Utility class for creating FlattenTexture instances
 */
class TextureFlattener {
public:
    /**
     * @brief Create a FlattenTexture instance
     * 
     * @param inputTexture The input texture array to flatten
     * @param name Name for the flattened texture (used for asset registration)
     * @return std::shared_ptr<FlattenTexture> The FlattenTexture instance
     */
    static std::shared_ptr<FlattenTexture> createFlattenTexture(std::shared_ptr<Texture> inputTexture, const std::string& name);

private:
    struct FlattenPushConstants {
        uint32_t inputTextureIndex;
        int layerCount;
        int layerWidth;
        int layerHeight;
        int tilesPerRow;
    };

    static void initializeSharedResources();
    static std::shared_ptr<Texture> createFlattenedTextureSpec(std::shared_ptr<Texture> inputTexture);

    // Shared resources
    static std::shared_ptr<Shader> s_flattenShader;
    static std::shared_ptr<Shader> s_flattenDepthShader;
    static std::shared_ptr<ComputePipeline> s_flattenPipeline;
    static std::shared_ptr<ComputePipeline> s_flattenDepthPipeline;
    static bool s_initialized;

    friend class FlattenTexture;
};

} 