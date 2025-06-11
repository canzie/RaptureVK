#pragma once

#include "Material.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"

#include <memory>
#include <string>
#include <atomic>
#include <mutex>


namespace Rapture {


class MaterialInstance {
    public:
        MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string& name = "");

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
            } else {
                RP_CORE_WARN("MaterialInstance::setParameter: Parameter ID '{}' not found for this material", parameterIdToString(id));
            }
        }

        bool isReady();

        void updateUniformBuffer(ParameterID id);

        std::unordered_map<ParameterID, MaterialParameter>& getParameterMap() { return m_parameterMap; }

    private:
        std::string m_name;
        std::shared_ptr<DescriptorSet> m_descriptorSet;
        bool m_isDirty = false;
        bool m_isReady = false;
        
        std::shared_ptr<BaseMaterial> m_baseMaterial;
        std::shared_ptr<UniformBuffer> m_uniformBuffer;

        std::unordered_map<ParameterID, MaterialParameter> m_parameterMap;


};


}


