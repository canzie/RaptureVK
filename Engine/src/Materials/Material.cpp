#include "Material.h"

#include "AssetManager/AssetManager.h"
#include "Logging/Log.h"
#include "Textures/Texture.h"

namespace Rapture {

bool MaterialManager::s_initialized = false;
uint32_t MaterialManager::s_defaultTextureIndex = 0;
std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> MaterialManager::s_materials;

BaseMaterial::BaseMaterial(const std::string &name, std::initializer_list<ParameterID> editableParams, const MaterialData &defaults)
    : m_name(name), m_editableParams(editableParams), m_defaults(defaults)
{
}

void MaterialManager::init()
{
    if (s_initialized) {
        RP_CORE_WARN("MaterialManager already initialized");
        return;
    }

    s_materials.clear();

    auto asset = AssetManager::importDefaultAsset(AssetType::TEXTURE);
    auto defaultTexture = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
    if (defaultTexture && defaultTexture->isReady()) {
        s_defaultTextureIndex = defaultTexture->getBindlessIndex();
    } else {
        RP_CORE_ERROR("Failed to get default white texture index");
        s_defaultTextureIndex = 0;
    }

    createDefaultMaterials();
    s_initialized = true;
}

void MaterialManager::shutdown()
{
    s_materials.clear();
    s_initialized = false;
}

void MaterialManager::createDefaultMaterials()
{
    uint32_t defTex = s_defaultTextureIndex;

    {
        MaterialData defaults = MaterialData::createDefault(defTex);
        auto pbr = std::make_shared<BaseMaterial>(
            "PBR",
            std::initializer_list<ParameterID>{ParameterID::ALBEDO, ParameterID::ROUGHNESS, ParameterID::METALLIC, ParameterID::AO,
                                               ParameterID::EMISSIVE, ParameterID::ALBEDO_MAP, ParameterID::NORMAL_MAP,
                                               ParameterID::METALLIC_ROUGHNESS_MAP, ParameterID::AO_MAP, ParameterID::EMISSIVE_MAP},
            defaults);
        s_materials["PBR"] = pbr;
    }

    {
        MaterialData defaults = MaterialData::createDefault(defTex);
        defaults.roughness = 0.9f;
        auto simple = std::make_shared<BaseMaterial>(
            "Simple", std::initializer_list<ParameterID>{ParameterID::ALBEDO, ParameterID::ALBEDO_MAP}, defaults);
        s_materials["Simple"] = simple;
    }

    {
        MaterialData defaults = MaterialData::createDefault(defTex);
        defaults.flags = MAT_FLAG_IS_TERRAIN;
        defaults.roughness = 0.9f;
        auto terrain = std::make_shared<BaseMaterial>(
            "Terrain",
            std::initializer_list<ParameterID>{ParameterID::ALBEDO, ParameterID::ROUGHNESS, ParameterID::METALLIC, ParameterID::AO,
                                               ParameterID::ALBEDO_MAP, ParameterID::NORMAL_MAP,
                                               ParameterID::METALLIC_ROUGHNESS_MAP, ParameterID::AO_MAP, ParameterID::TILING_SCALE,
                                               ParameterID::HEIGHT_BLEND, ParameterID::SLOPE_THRESHOLD, ParameterID::SPLAT_MAP},
            defaults);
        s_materials["Terrain"] = terrain;
    }

    RP_CORE_INFO("Created {} default materials", s_materials.size());
}

std::shared_ptr<BaseMaterial> MaterialManager::getMaterial(const std::string &name)
{
    if (!s_initialized) {
        RP_CORE_ERROR("MaterialManager not initialized");
        return nullptr;
    }

    auto it = s_materials.find(name);
    if (it == s_materials.end()) {
        RP_CORE_ERROR("Material '{}' not found", name);
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<BaseMaterial> MaterialManager::createMaterial(const std::string &name,
                                                              std::initializer_list<ParameterID> editableParams,
                                                              const MaterialData &defaults)
{
    if (!s_initialized) {
        RP_CORE_ERROR("MaterialManager not initialized");
        return nullptr;
    }

    if (s_materials.find(name) != s_materials.end()) {
        RP_CORE_WARN("Material '{}' already exists, returning existing", name);
        return s_materials[name];
    }

    auto material = std::make_shared<BaseMaterial>(name, editableParams, defaults);
    s_materials[name] = material;
    return material;
}

uint32_t MaterialManager::getDefaultTextureIndex()
{
    return s_defaultTextureIndex;
}

void MaterialManager::printMaterialNames()
{
    for (auto &[name, material] : s_materials) {
        RP_CORE_INFO("\t {}", name);
    }
}

} // namespace Rapture
