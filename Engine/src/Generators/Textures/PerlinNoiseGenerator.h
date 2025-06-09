#pragma once

#include "Textures/Texture.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Buffers/Descriptors/DescriptorSet.h"

#include <memory>


namespace Rapture {

class PerlinNoiseGenerator {
    public:

        static std::shared_ptr<Texture> generateNoise(int width, int height, int octaves, float persistence, float lacunarity, float scale = 8.0f);
    private:
        static std::unique_ptr<ComputePipeline> m_computePipeline;
        static std::shared_ptr<Shader> m_computeShader;
        
        // Helper methods
        static void initializeComputeResources();
        static std::shared_ptr<Texture> createOutputTexture(int width, int height);
        static std::unique_ptr<DescriptorSet> createStorageImageDescriptorSet(std::shared_ptr<Texture> outputTexture);
        static void transitionImageLayoutForCompute(VkCommandBuffer commandBuffer, VkImage image);
        static void transitionImageLayoutForSampling(VkCommandBuffer commandBuffer, VkImage image);
        static void dispatchComputeShader(VkCommandBuffer commandBuffer, int width, int height, int octaves, float persistence, float lacunarity, float scale);
};

}
