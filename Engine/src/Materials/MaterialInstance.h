#pragma once

#include "Material.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include <memory>
#include <string>


namespace Rapture {


class MaterialInstance {
    public:
        MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string& name = "");

        std::shared_ptr<BaseMaterial> getBaseMaterial() const { return m_baseMaterial; }

        const std::string& getName() const { return m_name; }

        VkDescriptorSet getDescriptorSet() const { return m_uniformBuffer->getDescriptorSet(); }

        template<typename T>
        void setParameter(ParameterID id, T value){
            if (m_parameterMap.find(id) != m_parameterMap.end()) {
                m_parameterMap[id].setValue<T>(value);
            } else {
                RP_CORE_WARN("MaterialInstance::setParameter: Parameter ID not found");
            }
        }

    private:
        std::string m_name;
        
        std::shared_ptr<BaseMaterial> m_baseMaterial;
        std::shared_ptr<UniformBuffer> m_uniformBuffer;

        std::unordered_map<ParameterID, MaterialParameter> m_parameterMap;


};


}


