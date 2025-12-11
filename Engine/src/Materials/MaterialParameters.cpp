#include "MaterialParameters.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Rapture {

// MaterialParameterMapper Implementation

std::unordered_map<MaterialParameterTypes, std::unordered_map<ParameterID, std::vector<std::string>>>
    MaterialParameterMapper::m_typeToIDsToStringsMap;
bool MaterialParameterMapper::m_isInitialized = false;

void MaterialParameterMapper::initializeMappings()
{
    if (m_isInitialized) {
        return;
    }

    // Albedo/Base Color mappings
    addParameterVariants(ParameterID::ALBEDO, MaterialParameterTypes::VEC3,
                         {"albedo", "albedo_color", "baseColor", "base_color", "diffuse", "diffuseColor", "diffuse_color", "color",
                          "mainColor", "main_color", "tint", "material_color"});

    addParameterVariants(ParameterID::ALBEDO_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"albedomap", "albedo_map", "albedoTexture", "albedo_texture", "basecolormap", "base_color_map",
                          "diffusemap", "diffuse_map", "diffuseTexture", "diffuse_texture", "colormap", "color_map", "maintexture",
                          "main_texture", "texture0", "tex0", "u_albedomap"});

    addParameterVariants(ParameterID::ALBEDO_MAP, MaterialParameterTypes::UINT,
                         {"albedomaphandle", "albedo_map_handle", "albedomapindex", "albedo_map_index", "albedohandle",
                          "albedoindex", "basecolormaphandle", "base_color_map_handle", "diffusemaphandle", "diffuse_map_handle",
                          "albedomapbindless", "basecolormapbindless"});

    // Metallic mappings
    addParameterVariants(
        ParameterID::METALLIC, MaterialParameterTypes::FLOAT,
        {"metallic", "metallicFactor", "metallic_factor", "metalness", "metal", "metallicValue", "metallic_value"});

    addParameterVariants(ParameterID::METALLIC_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"metallicMap", "metallic_map", "metallicTexture", "metallic_texture", "metalnessMap", "metalness_map",
                          "metalMap", "metal_map", "u_metallicMap"});

    addParameterVariants(ParameterID::METALLIC_MAP, MaterialParameterTypes::UINT,
                         {"metallicmaphandle", "metallic_map_handle", "metallicmapindex", "metallic_map_index", "metallichandle",
                          "metallicindex", "metalnessmaphandle", "metalness_map_handle", "metalmaphandle", "metal_map_handle",
                          "metallicmapbindless", "metalnessmapbindless"});

    // Roughness mappings
    addParameterVariants(ParameterID::ROUGHNESS, MaterialParameterTypes::FLOAT,
                         {"roughness", "roughnessFactor", "roughness_factor", "roughnessValue", "roughness_value",
                          "surface_roughness", "u_roughnessMap"});

    addParameterVariants(
        ParameterID::ROUGHNESS_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
        {"roughnessMap", "roughness_map", "roughnessTexture", "roughness_texture", "surface_roughness_map", "u_roughnessMap"});

    addParameterVariants(ParameterID::ROUGHNESS_MAP, MaterialParameterTypes::UINT,
                         {"roughnessmaphandle", "roughness_map_handle", "roughnessmapindex", "roughness_map_index",
                          "roughnesshandle", "roughnessindex", "roughnessmapbindless", "surface_roughness_map_handle"});

    addParameterVariants(ParameterID::METALLIC_ROUGHNESS_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"metallicRoughnessMap", "metallic_roughness_map", "metallic_roughness_texture",
                          "metallic_roughness_texture", "u_metallicRoughnessMap", "u_metallic_roughness_map"});

    addParameterVariants(ParameterID::METALLIC_ROUGHNESS_MAP, MaterialParameterTypes::UINT,
                         {"metallicroughnessmaphandle", "metallic_roughness_map_handle", "metallicroughnessmapindex",
                          "metallic_roughness_map_index", "metallicroughnesshandle", "metallicroughnessindex",
                          "metallicroughnessmapbindless", "metallic_roughness_bindless"});

    // Normal mappings
    addParameterVariants(ParameterID::NORMAL, MaterialParameterTypes::VEC3,
                         {"normal", "normalVector", "normal_vector", "surfaceNormal", "surface_normal"});

    addParameterVariants(ParameterID::NORMAL_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"normalMap", "normal_map", "normalTexture", "normal_texture", "bumpMap", "bump_map", "bumpTexture",
                          "bump_texture", "normalmap", "bump", "u_normalMap"});

    addParameterVariants(ParameterID::NORMAL_MAP, MaterialParameterTypes::UINT,
                         {"normalmaphandle", "normal_map_handle", "normalmapindex", "normal_map_index", "normalhandle",
                          "normalindex", "bumpmaphandle", "bump_map_handle", "normalmapbindless", "bumpmapbindless"});

    // Height/Displacement mappings
    addParameterVariants(ParameterID::HEIGHT_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"heightMap", "height_map", "heightTexture", "height_texture", "displacementMap", "displacement_map",
                          "displacementTexture", "displacement_texture", "parallaxMap", "parallax_map", "elevationMap",
                          "elevation_map", "u_heightMap"});

    addParameterVariants(ParameterID::HEIGHT_MAP, MaterialParameterTypes::UINT,
                         {"heightmaphandle", "height_map_handle", "heightmapindex", "height_map_index", "heighthandle",
                          "heightindex", "displacementmaphandle", "displacement_map_handle", "parallaxmaphandle",
                          "parallax_map_handle", "heightmapbindless", "displacementmapbindless"});

    // Ambient Occlusion mappings
    addParameterVariants(ParameterID::AO_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"aoMap", "ao_map", "aoTexture", "ao_texture", "ambientOcclusionMap", "ambient_occlusion_map",
                          "occlusionMap", "occlusion_map", "ambientMap", "ambient_map", "u_aoMap"});

    addParameterVariants(ParameterID::AO_MAP, MaterialParameterTypes::UINT,
                         {"aomaphandle", "ao_map_handle", "aomapindex", "ao_map_index", "aohandle", "aoindex",
                          "ambientocclusionmaphandle", "ambient_occlusion_map_handle", "occlusionmaphandle", "occlusion_map_handle",
                          "aomapbindless", "ambientocclusionmapbindless"});

    // Ambient Occlusion mappings
    addParameterVariants(ParameterID::AO, MaterialParameterTypes::FLOAT, {"ao", "ambientOcclusion", "ambient_occlusion"});

    // Emissive mappings
    addParameterVariants(ParameterID::EMISSIVE, MaterialParameterTypes::VEC3,
                         {"emissive", "emissiveColor", "emissive_color", "emission", "emissionColor", "emission_color", "glow",
                          "glowColor", "glow_color"});

    addParameterVariants(ParameterID::EMISSIVE_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"emissiveMap", "emissive_map", "emissiveTexture", "emissive_texture", "emissionMap", "emission_map",
                          "glowMap", "glow_map", "u_emissiveMap"});

    addParameterVariants(ParameterID::EMISSIVE_MAP, MaterialParameterTypes::UINT,
                         {"emissivemaphandle", "emissive_map_handle", "emissivemapindex", "emissive_map_index", "emissivehandle",
                          "emissiveindex", "emissionmaphandle", "emission_map_handle", "glowmaphandle", "glow_map_handle",
                          "emissivemapbindless", "emissionmapbindless"});

    // Specular mappings
    addParameterVariants(ParameterID::SPECULAR, MaterialParameterTypes::VEC3,
                         {"specular", "specularColor", "specular_color", "specularFactor", "specular_factor"});

    addParameterVariants(ParameterID::SPECULAR_MAP, MaterialParameterTypes::COMBINED_IMAGE_SAMPLER,
                         {"specularMap", "specular_map", "specularTexture", "specular_texture"});

    addParameterVariants(ParameterID::SPECULAR_MAP, MaterialParameterTypes::UINT,
                         {"specularmaphandle", "specular_map_handle", "specularmapindex", "specular_map_index", "specularhandle",
                          "specularindex", "specularmapbindless", "specular_bindless"});

    m_isInitialized = true;
}

ParameterID MaterialParameterMapper::getParameterID(const std::string &rawName, MaterialParameterTypes type)
{
    if (!m_isInitialized) {
        initializeMappings();
    }

    if (type == MaterialParameterTypes::UNKNOWN) {
        return ParameterID::UNKNOWN;
    }

    for (const auto &[id, variants] : m_typeToIDsToStringsMap[type]) {
        if (std::find(variants.begin(), variants.end(), toLowerCase(rawName)) != variants.end()) {
            return id;
        }
    }

    return ParameterID::UNKNOWN;
}

void MaterialParameterMapper::addParameterVariants(ParameterID id, MaterialParameterTypes type,
                                                   const std::vector<std::string> &variants)
{

    std::vector<std::string> lowerCaseVariants;
    for (const auto &variant : variants) {
        lowerCaseVariants.push_back(toLowerCase(variant));
    }

    m_typeToIDsToStringsMap[type][id] = lowerCaseVariants;
}

std::string MaterialParameterMapper::toLowerCase(const std::string &str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Helper Functions

std::string parameterTypeToString(MaterialParameterTypes type)
{
    switch (type) {
    case MaterialParameterTypes::FLOAT:
        return "float";
    case MaterialParameterTypes::INT:
        return "int";
    case MaterialParameterTypes::UINT:
        return "uint";
    case MaterialParameterTypes::BOOL:
        return "bool";
    case MaterialParameterTypes::VEC2:
        return "vec2";
    case MaterialParameterTypes::VEC3:
        return "vec3";
    case MaterialParameterTypes::VEC4:
        return "vec4";
    case MaterialParameterTypes::MAT3:
        return "mat3";
    case MaterialParameterTypes::MAT4:
        return "mat4";
    case MaterialParameterTypes::TEXTURE:
        return "texture";
    case MaterialParameterTypes::COMBINED_IMAGE_SAMPLER:
        return "COMBINED_IMAGE_SAMPLER";
    case MaterialParameterTypes::SAMPLER:
        return "sampler";
    default:
        return "unknown";
    }
}

std::string parameterIdToString(ParameterID id)
{
    switch (id) {
    case ParameterID::ALBEDO:
        return "ALBEDO";
    case ParameterID::ALBEDO_MAP:
        return "ALBEDO_MAP";
    case ParameterID::METALLIC:
        return "METALLIC";
    case ParameterID::METALLIC_MAP:
        return "METALLIC_MAP";
    case ParameterID::ROUGHNESS:
        return "ROUGHNESS";
    case ParameterID::ROUGHNESS_MAP:
        return "ROUGHNESS_MAP";
    case ParameterID::METALLIC_ROUGHNESS_MAP:
        return "METALLIC_ROUGHNESS_MAP";
    case ParameterID::NORMAL:
        return "NORMAL";
    case ParameterID::NORMAL_MAP:
        return "NORMAL_MAP";
    case ParameterID::HEIGHT_MAP:
        return "HEIGHT_MAP";
    case ParameterID::AO_MAP:
        return "AO_MAP";
    case ParameterID::EMISSIVE:
        return "EMISSIVE";
    case ParameterID::EMISSIVE_MAP:
        return "EMISSIVE_MAP";
    case ParameterID::SPECULAR:
        return "SPECULAR";
    case ParameterID::SPECULAR_MAP:
        return "SPECULAR_MAP";
    default:
        return "UNKNOWN";
    }
}

MaterialParameterTypes stringToMaterialParameterType(const std::string &str)
{
    if (str == "float")
        return MaterialParameterTypes::FLOAT;
    if (str == "int")
        return MaterialParameterTypes::INT;
    if (str == "uint")
        return MaterialParameterTypes::UINT;
    if (str == "bool")
        return MaterialParameterTypes::BOOL;
    if (str == "vec2")
        return MaterialParameterTypes::VEC2;
    if (str == "vec3")
        return MaterialParameterTypes::VEC3;
    if (str == "vec4")
        return MaterialParameterTypes::VEC4;
    if (str == "mat3")
        return MaterialParameterTypes::MAT3;
    if (str == "mat4")
        return MaterialParameterTypes::MAT4;
    if (str == "texture")
        return MaterialParameterTypes::TEXTURE;
    if (str == "COMBINED_IMAGE_SAMPLER")
        return MaterialParameterTypes::COMBINED_IMAGE_SAMPLER;
    if (str == "sampler")
        return MaterialParameterTypes::SAMPLER;
    return MaterialParameterTypes::UNKNOWN;
}

MaterialParameter::MaterialParameter(const DescriptorParamInfo &info, uint32_t binding)
{
    m_info.parameterId = MaterialParameterMapper::getParameterID(info.name, stringToMaterialParameterType(info.type));
    m_info.type = stringToMaterialParameterType(info.type);
    m_info.parameterName = info.name;
    m_info.binding = binding;
    m_info.size = info.size;
    m_info.offset = info.offset;

    if (m_info.parameterId == ParameterID::UNKNOWN) {
        RP_CORE_ERROR("MaterialParameter::MaterialParameter - unknown parameter id: {0}", info.name);
    }
    if (m_info.type == MaterialParameterTypes::UNKNOWN) {
        RP_CORE_ERROR("MaterialParameter::MaterialParameter - unknown parameter type: {0}", info.name);
    }

    // Initialize the variant with the appropriate type
    switch (m_info.type) {
    case MaterialParameterTypes::FLOAT:
        m_value = 0.0f;
        break;
    case MaterialParameterTypes::INT:
        m_value = 0;
        break;
    case MaterialParameterTypes::UINT:
        m_value = 0u;
        break;
    case MaterialParameterTypes::UINT64:
        m_value = 0ull;
        break;
    case MaterialParameterTypes::BOOL:
        m_value = false;
        break;
    case MaterialParameterTypes::VEC2:
        m_value = glm::vec2(0.0f);
        break;
    case MaterialParameterTypes::VEC3:
        m_value = glm::vec3(0.0f);
        break;
    case MaterialParameterTypes::VEC4:
        m_value = glm::vec4(0.0f);
        break;
    case MaterialParameterTypes::MAT3:
        m_value = glm::mat3(1.0f);
        break;
    case MaterialParameterTypes::MAT4:
        m_value = glm::mat4(1.0f);
        break;
    case MaterialParameterTypes::COMBINED_IMAGE_SAMPLER:
        m_value = nullptr;
        break;
    default:
        m_value = std::monostate{};
        break;
    }
}

} // namespace Rapture