#include "Material.h"

#include "Logging/Log.h"

#include "AssetManager/AssetManager.h"

#include "Buffers/CommandBuffers/CommandPool.h"

#include "WindowContext/Application.h"

namespace Rapture {

// Define the static member variable
VkDescriptorPool BaseMaterial::s_DescriptorPool = VK_NULL_HANDLE;

std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> MaterialManager::s_materials;

    BaseMaterial::BaseMaterial(std::shared_ptr<Shader> shader, const std::string &name)
    : m_shader(shader)
    {


        if (shader->getDescriptorSetLayouts().size() < 1) {
            throw std::runtime_error("Material::BaseMaterial - shader has no descriptor set layout for a material!");
        }

        m_descriptorSetLayout = shader->getDescriptorSetLayouts()[1];
        // get descriptor info
        // assume only 1 descriptor in that set for now
        auto info = m_shader->getMaterialSets()[0];

        if (name.empty()) {
            m_name = info.name;
        } else {
            m_name = name;
        }

        m_sizeBytes = 0;

        // get descriptor set layout
        for (auto& parameter : info.params) {
            auto param = MaterialParameter(parameter);
            if (param.m_info.parameterId != ParameterID::UNKNOWN) {
                m_parameterMap[param.m_info.parameterId] = param;
                m_sizeBytes += param.m_info.size;
            } else {
                RP_CORE_ERROR("Material::BaseMaterial - unknown parameter id: {0} and type: {1}", parameter.name, parameter.type);
            }
        }
        
        if (m_sizeBytes == 0) {
            RP_CORE_ERROR("Material::BaseMaterial - no valid parameters found in material set {0}", info.name);
            throw std::runtime_error("Material::BaseMaterial - no valid parameters found in material set {0}");
        }

    }

    void MaterialManager::init()
    {
        s_materials.clear();

        auto& app = Application::getInstance();
        auto device = app.getVulkanContext().getLogicalDevice();

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        poolInfo.maxSets = 100;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &BaseMaterial::s_DescriptorPool) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to create descriptor pool!");
            throw std::runtime_error("failed to create descriptor pool!");
        }

        // create material for simple shader
        // load shader
        const std::filesystem::path vertShaderPath = "E:/Dev/Games/RaptureVK/Engine/assets/shaders/SPIRV/default.vs.spv";

        auto [shader, handle] = AssetManager::importAsset<Shader>(vertShaderPath);
        // create material in a try catch
        try {
            auto material = std::make_shared<BaseMaterial>(shader);
            auto name = material->getName();
            if (name.empty()) {
                name = std::to_string(handle) + "_material";
            }
            s_materials[name] = material;
        } catch (const std::exception& e) {
            RP_CORE_ERROR("MaterialManager::init - {}", e.what());
        }
    }

    void MaterialManager::shutdown()
    {
        auto& app = Application::getInstance();
        auto device = app.getVulkanContext().getLogicalDevice();

        // destroy all materials
        s_materials.clear();

        vkDestroyDescriptorPool(device, BaseMaterial::s_DescriptorPool, nullptr);
    }

    std::shared_ptr<BaseMaterial> MaterialManager::getMaterial(const std::string &name)
    {
        if (s_materials.find(name) == s_materials.end()) {
            RP_CORE_ERROR("MaterialManager::getMaterial - material '{0}' not found!", name);
            return nullptr;
        }
        return s_materials[name];
    }

    void MaterialManager::printMaterialNames()
    {
        for (auto& [name, material] : s_materials) {
            RP_CORE_INFO("\t MaterialManager::printMaterialNames - {0}", name);
        }
    }
}