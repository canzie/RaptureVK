#include "MaterialInstance.h"

#include "WindowContext/Application.h"

namespace Rapture {


MaterialInstance::MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name)
    : m_baseMaterial(material)
{
    auto& app = Application::getInstance();
    auto allocator = app.getVulkanContext().getVmaAllocator();

    if (name.empty()) {
        m_name = material->getName() + "_instance";
    } else {
        m_name = name;
    }

    RP_CORE_INFO("Creating MaterialInstance for {0} with size {1}", m_name, material->getSizeBytes());

    if (allocator == nullptr) {
        RP_CORE_ERROR("MaterialInstance::MaterialInstance - allocator is nullptr!");
        throw std::runtime_error("MaterialInstance::MaterialInstance - allocator is nullptr!");
    }

    // Create uniform buffer
    m_uniformBuffer = std::make_shared<UniformBuffer>(material->getSizeBytes(), BufferUsage::DYNAMIC, allocator, nullptr);

    // Create descriptor set
    m_uniformBuffer->createDescriptorSet(material->getDescriptorSetLayout(), material->s_DescriptorPool, 0);
    
    m_parameterMap = material->getTemplateParameters();

    for (auto& [id, param] : m_parameterMap) {
        m_uniformBuffer->addData(param.asRaw(), param.m_info.size, param.m_info.offset);
        
    }

}

}