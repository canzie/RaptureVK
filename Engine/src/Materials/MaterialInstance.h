#pragma once

#include "Material.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Materials/MaterialParameters.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include <memory>
#include <string>
#include <atomic>
#include <mutex>


namespace Rapture {


class MaterialInstance {
    public:
        MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string& name = "");
        ~MaterialInstance();

        std::shared_ptr<BaseMaterial> getBaseMaterial() const { return m_baseMaterial; }

        const std::string& getName() const { return m_name; }

        VkDescriptorSet getDescriptorSet();

        // call this when you added a new parameter value that was not set before
        // e.g.: textures start as nullptr, so when you set them the descriptor set needs to know about this VkImageView
        void updateDescriptorSet();

        template<typename T>
        void setParameter(ParameterID id, T value){
            if (m_parameterMap.find(id) != m_parameterMap.end()) {
                m_parameterMap[id].setValue<T>(value);
                updateUniformBuffer(id);
                
                // Update material flags when setting texture parameters
                if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) {
                    m_flagsDirty = true;
                }
            } else {
                RP_CORE_WARN("MaterialInstance::setParameter: Parameter ID '{}' not found for this material", parameterIdToString(id));
            }
        }

       MaterialParameter getParameter(ParameterID id); 
        

        bool isReady();

        void updateUniformBuffer(ParameterID id);

        std::unordered_map<ParameterID, MaterialParameter>& getParameterMap() { return m_parameterMap; }

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

        uint32_t m_bindlessUniformBufferIndex;
        //uint32_t m_bindlessTextureIndex;
        
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


};


}


