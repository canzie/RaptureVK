#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Buffers/Buffers.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Shaders/Shader.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"



#include "DDGICommon.h"

namespace Rapture {


class DynamicDiffuseGI {
public:
    DynamicDiffuseGI();
    ~DynamicDiffuseGI();

    void populateProbes(std::shared_ptr<Scene> scene);
    void populateProbesCompute(std::shared_ptr<Scene> scene);

    std::shared_ptr<Texture> getRadianceTexture() { return m_RadianceTexture; } 
    std::shared_ptr<Texture> getVisibilityTexture() { return m_VisibilityTexture; }
    std::shared_ptr<Texture> getRadianceTextureFlattened() { return m_IrradianceTextureFlattened; } 
    std::shared_ptr<Texture> getVisibilityTextureFlattened() { return m_DistanceTextureFlattened; } 

    std::vector<glm::vec3>& getDebugProbePositions() { return m_DebugProbePositions; }

    std::shared_ptr<UniformBuffer> getProbeInfoBuffer() { return m_ProbeInfoBuffer; }
    uint32_t getProbesPerRow() { return m_probesPerRow; }

private:
    void castRays(std::shared_ptr<Scene> scene);
    void blendTextures();

    //int createBufferMetadata(std::shared_ptr<VertexArray> vao);
    int getBufferMetadataIndex(uint32_t vaoID);
    void readDebugBuffer();
    void initTextures();
    void updateSunProperties(std::shared_ptr<Scene> scene);
    void initProbeInfoBuffer();

    void createPipelines();

private:
    std::shared_ptr<Shader> m_DDGI_ProbeTraceShader;
    std::shared_ptr<Shader> m_DDGI_ProbeIrradianceBlendingShader;
    std::shared_ptr<Shader> m_DDGI_ProbeDistanceBlendingShader;
    std::shared_ptr<Shader> m_Flatten2dArrayShader;

    ProbeVolume m_ProbeVolume;
    
    SunProperties m_SunShadowProps;

    
    std::shared_ptr<StorageBuffer> m_MeshInfoBuffer;

    //std::shared_ptr<ShaderStorageBuffer> m_DebugBuffer;

    std::shared_ptr<UniformBuffer> m_SunLightBuffer;
    std::shared_ptr<UniformBuffer> m_ProbeInfoBuffer;


    // is actually irradiance but iam retarted, will need to update this everywhere :(
    std::shared_ptr<Texture> m_RadianceTexture;
    std::shared_ptr<Texture> m_VisibilityTexture;

    std::shared_ptr<Texture> m_PrevRadianceTexture;
    std::shared_ptr<Texture> m_PrevVisibilityTexture;

    std::shared_ptr<Texture> m_RayDataTexture;

    std::shared_ptr<Texture> m_IrradianceTextureFlattened;
    std::shared_ptr<Texture> m_DistanceTextureFlattened;


    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;


    // used to alternate between the textures each frame
    bool m_isEvenFrame;

    bool m_isPopulated;

    float m_Hysteresis;

    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    
};

}

