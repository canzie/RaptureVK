#include "Material.h"

#include "Logging/Log.h"

#include "AssetManager/AssetManager.h"

#include "WindowContext/Application.h"

#include "Shaders/Shader.h"
#include <string>

namespace Rapture {

bool MaterialManager::s_initialized = false;


std::unordered_map<std::string, std::shared_ptr<BaseMaterial>>
    MaterialManager::s_materials;

BaseMaterial::BaseMaterial(std::shared_ptr<Shader> shader,
                        const std::string &name)
    : m_shader(shader) {

    if (shader->getDescriptorSetLayouts().size() < 1) {
        throw std::runtime_error("Material::BaseMaterial - shader has no "
                                "descriptor set layout for a material!");
    }



    m_descriptorSetLayout = shader->getDescriptorSetLayouts()[static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::MATERIAL)];

    auto infos = m_shader->getMaterialSets();

    if (name.empty()) {
        m_name = infos[0].name;
    } else {
        m_name = name;
    }

    m_sizeBytes = 0;
    for (auto &info : infos) {
        // get descriptor set layout
        for (auto &parameter : info.params) {
        auto param = MaterialParameter(parameter, info.binding);

            if (param.m_info.parameterId == ParameterID::ALBEDO) {
                param.setValue(glm::vec3(1.0f, 1.0f, 1.0f));
            }

        if (param.m_info.parameterId != ParameterID::UNKNOWN) {
            m_parameterMap[param.m_info.parameterId] = param;
            // Ensure the overall buffer size encompasses this parameter (offset + its size)
            uint32_t endOffset = static_cast<uint32_t>(param.m_info.offset + param.m_info.size);
            if (endOffset > m_sizeBytes) {
                m_sizeBytes = endOffset;
            }
        } else {
            RP_CORE_ERROR(
                "Material::BaseMaterial - unknown parameter id: {0} and type: {1}",
                parameter.name, parameter.type);
        }
        }
    }

    // Align size to 16 bytes for std140 compliance
    if (m_sizeBytes % 16 != 0) {
        m_sizeBytes = (m_sizeBytes + 15) & ~15u; // round up to next multiple of 16
    }

    if (m_sizeBytes == 0) {
        RP_CORE_ERROR("Material::BaseMaterial - no valid parameters found in "
                    "material set {0}",
                    infos[0].name);
        throw std::runtime_error("Material::BaseMaterial - no valid parameters "
                                "found in material set {0}");
    }
}

void MaterialManager::init() {
  s_materials.clear();

  auto &app = Application::getInstance();
  auto &project = app.getProject();

  auto shaderPath = project.getProjectShaderDirectory();

  // create material for simple shader
  // load shader
  const std::string basicShaderPath = "SPIRV/default.vs.spv";

  auto [basicShader, basicShaderHandle] =
      AssetManager::importAsset<Shader>(shaderPath / basicShaderPath);
  // create material in a try catch
  try {
    auto material = std::make_shared<BaseMaterial>(basicShader, "material");
    auto name = material->getName();
    if (name.empty()) {
      name = std::to_string(basicShaderHandle) + "_material";
    }
    s_materials[name] = material;
  } catch (const std::exception &e) {
    RP_CORE_ERROR("MaterialManager::init - {}", e.what());
  }

  const std::string pbrShaderPath = "SPIRV/GBuffer.vs.spv";

  auto [pbrShader, pbrShaderHandle] =
      AssetManager::importAsset<Shader>(shaderPath / pbrShaderPath);
  // create material in a try catch
  try {
    auto material = std::make_shared<BaseMaterial>(pbrShader, "PBR");
    auto name = material->getName();
    if (name.empty()) {
      name = std::to_string(pbrShaderHandle) + "_material";
    }
    s_materials[name] = material;
  } catch (const std::exception &e) {
    RP_CORE_ERROR("MaterialManager::init - {}", e.what());
  }

  s_initialized = true;
}

void MaterialManager::shutdown() {
  auto &app = Application::getInstance();
  auto device = app.getVulkanContext().getLogicalDevice();

  // destroy all materials
  s_materials.clear();

  s_initialized = false;
}

std::shared_ptr<BaseMaterial>
MaterialManager::getMaterial(const std::string &name) {
  if (!s_initialized) {
    RP_CORE_ERROR(
        "MaterialManager::getMaterial - material manager not initialized!");
    return nullptr;
  }

  if (s_materials.find(name) == s_materials.end()) {
    RP_CORE_ERROR("MaterialManager::getMaterial - material '{0}' not found!",
                  name);
    return nullptr;
  }
  return s_materials[name];
}

void MaterialManager::printMaterialNames() {
  for (auto &[name, material] : s_materials) {
    RP_CORE_INFO("\t MaterialManager::printMaterialNames - {0}", name);
  }
}
} // namespace Rapture
