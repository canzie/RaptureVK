#pragma once

#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Events/AssetEvents.h" // Publish material change events
#include "Material.h"
#include "Materials/MaterialParameters.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Rapture {

struct PendingTexture {
    ParameterID parameterId;
    std::shared_ptr<Texture> texture;
};

class MaterialInstance {
  public:
    MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name = "");
    ~MaterialInstance();

    std::shared_ptr<BaseMaterial> getBaseMaterial() const { return m_baseMaterial; }

    const std::string &getName() const { return m_name; }

    template <typename T> void setParameter(ParameterID id, T value)
    {
        if (m_parameterMap.find(id) != m_parameterMap.end()) {
            m_parameterMap[id].setValue<T>(value);
            updateUniformBuffer(id);

            // Notify listeners that this material instance has changed
            Rapture::AssetEvents::onMaterialInstanceChanged().publish(this);

            // Update material flags when setting texture parameters
            if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) {
                m_flagsDirty = true;
            }
        } else {
            RP_CORE_WARN("MaterialInstance::setParameter: Parameter ID '{}' not found for this material", parameterIdToString(id));
        }
    }

    void setParameter(ParameterID id, std::shared_ptr<Texture> texture);

    MaterialParameter getParameter(ParameterID id);

    void updatePendingTextures();

    void updateUniformBuffer(ParameterID id);

    std::unordered_map<ParameterID, MaterialParameter> &getParameterMap() { return m_parameterMap; }

    // Get the cached material flags
    uint32_t getMaterialFlags() const;

    // Force recalculation of material flags (useful after bulk parameter changes)
    void recalculateMaterialFlags();

    // Get the bindless index for this material's uniform buffer
    uint32_t getBindlessIndex() const { return m_bindlessUniformBufferIndex; }

  private:
    std::string m_name;
    std::shared_ptr<DescriptorSet> m_descriptorSet;
    bool m_isDirty = false;
    bool m_isReady = false;

    std::vector<PendingTexture> m_pendingTextures;
    std::mutex m_pendingTexturesMutex;

    uint32_t m_bindlessUniformBufferIndex;
    // uint32_t m_bindlessTextureIndex;

    std::shared_ptr<BaseMaterial> m_baseMaterial;
    std::shared_ptr<UniformBuffer> m_uniformBuffer;

    std::unordered_map<ParameterID, MaterialParameter> m_parameterMap;

    // Material flags cache
    mutable uint32_t m_materialFlags = 0;
    mutable bool m_flagsDirty = true;

    // Calculate material flags from current parameters
    uint32_t calculateMaterialFlags() const;

    // Helper to check if a texture parameter is valid (not null)
    bool hasValidTexture(ParameterID id) const;

    // Helper to check if a parameter ID represents a texture
    bool isTextureParameter(ParameterID id) const;
};

} // namespace Rapture
