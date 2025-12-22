#ifndef RAPTURE__RADIANCE_CASCADES_H
#define RAPTURE__RADIANCE_CASCADES_H

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Pipelines/ComputePipeline.h"
#include "Scenes/Scene.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include "Utils/TextureFlattener.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"

#include "rc_common.h"

namespace Rapture {

class RadianceCascades {
  public:
    RadianceCascades(uint32_t framesInFlight);
    ~RadianceCascades();

    void update(std::shared_ptr<Scene> scene, uint32_t frameIndex);

    std::shared_ptr<Texture> getRadianceTexture(uint32_t cascade = 0)
    {
        return (cascade < m_radianceTextures.size()) ? m_radianceTextures[cascade] : nullptr;
    }

    std::shared_ptr<Texture> getRadianceTextureFlattened(uint32_t cascade = 0)
    {
        if (cascade < m_radianceTexturesFlattened.size() && m_radianceTexturesFlattened[cascade]) {
            return m_radianceTexturesFlattened[cascade]->getFlattenedTexture();
        }
        return nullptr;
    }

    std::shared_ptr<UniformBuffer> getVolumeUniformBuffer() { return m_volumeUBO; }

    const RadianceCascadeConfig &getConfig() const { return m_config; }
    void setConfig(const RadianceCascadeConfig &config);

    void onResize(uint32_t framesInFlight);

  private:
    void createPipelines();
    void initTextures();
    void initVolumeUBO();
    void updateVolumeUBO();
    void traceCascades(std::shared_ptr<Scene> scene, uint32_t frameIndex);

  private:
    RadianceCascadeConfig m_config;
    RCVolumeGPU m_volumeGPU;
    bool m_isVolumeDirty = true;

    std::shared_ptr<Shader> m_traceShader;
    std::shared_ptr<ComputePipeline> m_tracePipeline;

    std::shared_ptr<UniformBuffer> m_volumeUBO;

    std::vector<std::shared_ptr<Texture>> m_radianceTextures;
    std::vector<std::shared_ptr<FlattenTexture>> m_radianceTexturesFlattened;

    std::shared_ptr<DescriptorSet> m_traceDescriptorSet;

    std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
    std::shared_ptr<VulkanQueue> m_computeQueue;

    uint32_t m_framesInFlight;
    bool m_isFirstFrame = true;

    VmaAllocator m_allocator;
};

} // namespace Rapture

#endif // RAPTURE__RADIANCE_CASCADES_H
