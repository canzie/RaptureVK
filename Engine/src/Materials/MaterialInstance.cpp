#include "MaterialInstance.h"

#include "WindowContext/Application.h"
#include "Textures/Texture.h"
#include "AssetManager/AssetManager.h"

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

    //RP_CORE_INFO("Creating MaterialInstance for {0} with size {1}", m_name, material->getSizeBytes());

    if (allocator == nullptr) {
        RP_CORE_ERROR("MaterialInstance::MaterialInstance - allocator is nullptr!");
        throw std::runtime_error("MaterialInstance::MaterialInstance - allocator is nullptr!");
    }

    // Create uniform buffer
    m_uniformBuffer = std::make_shared<UniformBuffer>(material->getSizeBytes(), BufferUsage::DYNAMIC, allocator, nullptr);

    
    m_parameterMap = material->getTemplateParameters();


    DescriptorSetBindings bindings;
    bindings.layout = material->getDescriptorSetLayout();
    



    for (auto& [id, param] : m_parameterMap) {
        if (param.m_info.type == MaterialParameterTypes::COMBINED_IMAGE_SAMPLER) {
            // Add texture binding with default white texture initially
            DescriptorSetBinding binding;
            binding.binding = param.m_info.binding;
            binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.count = 1;
            binding.resource = AssetManager::importDefaultAsset<Texture>(AssetType::Texture).first; // Use default white texture

            bindings.bindings.push_back(binding);
        } else {
            // Add uniform buffer binding
            DescriptorSetBinding binding;
            binding.binding = param.m_info.binding;
            binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.count = 1;
            binding.resource = m_uniformBuffer;

            bindings.bindings.push_back(binding);

            m_uniformBuffer->addData(param.asRaw(), param.m_info.size, param.m_info.offset);
        }
    }

    m_descriptorSet = std::make_shared<DescriptorSet>(bindings);
    



}

VkDescriptorSet MaterialInstance::getDescriptorSet()
{


    return m_descriptorSet->getDescriptorSet(); 
}

void MaterialInstance::updateDescriptorSet()
{

    DescriptorSetBindings bindings;
    bindings.layout = m_baseMaterial->getDescriptorSetLayout();
    

    for (auto& [id, param] : m_parameterMap) {
        if (param.m_info.type == MaterialParameterTypes::COMBINED_IMAGE_SAMPLER) {
            std::shared_ptr<Texture> texture = nullptr;
            
            if (std::holds_alternative<std::shared_ptr<Texture>>(param.m_value)) {
                texture = std::get<std::shared_ptr<Texture>>(param.m_value);
                
            }
            
            // Use default white texture if no texture is set
            if (texture == nullptr) {
                texture = AssetManager::importDefaultAsset<Texture>(AssetType::Texture).first;
            }
            
            DescriptorSetBinding binding;
            binding.binding = param.m_info.binding;
            binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.count = 1;
            binding.resource = texture;

            bindings.bindings.push_back(binding);
        } else {
            DescriptorSetBinding binding;
            binding.binding = param.m_info.binding;
            binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.count = 1;
            binding.resource = m_uniformBuffer;

            bindings.bindings.push_back(binding);
        }

        
    }

    m_descriptorSet->updateDescriptorSet(bindings);
}



bool MaterialInstance::isReady()
{

    if (m_isReady) {
        return true;
    }

    for (auto& [id, param] : m_parameterMap) {

        if (param.m_info.type == MaterialParameterTypes::COMBINED_IMAGE_SAMPLER) {
            std::shared_ptr<Texture> texture = nullptr;
            
            if (std::holds_alternative<std::shared_ptr<Texture>>(param.m_value)) {
                texture = std::get<std::shared_ptr<Texture>>(param.m_value);
            }
            
            // If no texture is assigned, we'll use default texture (which should always be ready)
            if (texture == nullptr) {
                texture = AssetManager::importDefaultAsset<Texture>(AssetType::Texture).first;
            }
            
            // Check if the texture (either assigned or default) is ready
            if (texture && !texture->isReadyForSampling()) {
                return false;
            }
        }
    }

    updateDescriptorSet();


    m_isReady = true;
    return true;
}

void MaterialInstance::updateUniformBuffer(ParameterID id)
{
    m_uniformBuffer->addData(m_parameterMap[id].asRaw(), m_parameterMap[id].m_info.size, m_parameterMap[id].m_info.offset);
}
}