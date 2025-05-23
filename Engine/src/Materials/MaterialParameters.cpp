#include "MaterialParameters.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Rapture {

// MaterialParameterMapper Implementation

std::unordered_map<MaterialParameterTypes, std::unordered_map<ParameterID, std::vector<std::string>>> MaterialParameterMapper::m_typeToIDsToStringsMap;
bool MaterialParameterMapper::m_isInitialized = false;

void MaterialParameterMapper::initializeMappings() {
    if (m_isInitialized) {
        return;
    }

    // Albedo/Base Color mappings
    addParameterVariants(ParameterID::ALBEDO, MaterialParameterTypes::VEC3, {
        "albedo", "albedo_color", "baseColor", "base_color", "diffuse", "diffuseColor", 
        "diffuse_color", "color", "mainColor", "main_color", "tint", "material_color"
    });
    
    addParameterVariants(ParameterID::ALBEDO_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "albedoMap", "albedo_map", "albedoTexture", "albedo_texture", "baseColorMap", 
        "base_color_map", "diffuseMap", "diffuse_map", "diffuseTexture", "diffuse_texture", 
        "colorMap", "color_map", "mainTexture", "main_texture", "texture0", "tex0"
    });
    
    // Metallic mappings
    addParameterVariants(ParameterID::METALLIC, MaterialParameterTypes::FLOAT, {
        "metallic", "metallicFactor", "metallic_factor", "metalness", "metal", 
        "metallicValue", "metallic_value"
    });
    
    addParameterVariants(ParameterID::METALLIC_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "metallicMap", "metallic_map", "metallicTexture", "metallic_texture", 
        "metalnessMap", "metalness_map", "metalMap", "metal_map"
    });
    
    // Roughness mappings
    addParameterVariants(ParameterID::ROUGHNESS, MaterialParameterTypes::FLOAT, {
        "roughness", "roughnessFactor", "roughness_factor", "roughnessValue", 
        "roughness_value", "surface_roughness"
    });
    
    addParameterVariants(ParameterID::ROUGHNESS_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "roughnessMap", "roughness_map", "roughnessTexture", "roughness_texture",
        "surface_roughness_map"
    });
    
    // Normal mappings
    addParameterVariants(ParameterID::NORMAL, MaterialParameterTypes::VEC3, {
        "normal", "normalVector", "normal_vector", "surfaceNormal", "surface_normal"
    });
    
    addParameterVariants(ParameterID::NORMAL_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "normalMap", "normal_map", "normalTexture", "normal_texture", "bumpMap", 
        "bump_map", "bumpTexture", "bump_texture", "normalmap", "bump"
    });
    
    // Height/Displacement mappings
    addParameterVariants(ParameterID::HEIGHT_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "heightMap", "height_map", "heightTexture", "height_texture", "displacementMap", 
        "displacement_map", "displacementTexture", "displacement_texture", "parallaxMap", 
        "parallax_map", "elevationMap", "elevation_map"
    });
    
    // Ambient Occlusion mappings
    addParameterVariants(ParameterID::AO_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "aoMap", "ao_map", "aoTexture", "ao_texture", "ambientOcclusionMap", 
        "ambient_occlusion_map", "occlusionMap", "occlusion_map", "ambientMap", "ambient_map"
    });
    
    // Emissive mappings
    addParameterVariants(ParameterID::EMISSIVE, MaterialParameterTypes::VEC3, {
        "emissive", "emissiveColor", "emissive_color", "emission", "emissionColor", 
        "emission_color", "glow", "glowColor", "glow_color"
    });
    
    addParameterVariants(ParameterID::EMISSIVE_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "emissiveMap", "emissive_map", "emissiveTexture", "emissive_texture", 
        "emissionMap", "emission_map", "glowMap", "glow_map"
    });
    
    // Specular mappings
    addParameterVariants(ParameterID::SPECULAR, MaterialParameterTypes::VEC3, {
        "specular", "specularColor", "specular_color", "specularFactor", "specular_factor"
    });
    
    addParameterVariants(ParameterID::SPECULAR_MAP, MaterialParameterTypes::TEXTURE_2D, {
        "specularMap", "specular_map", "specularTexture", "specular_texture"
    });

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

    for (const auto& [id, variants] : m_typeToIDsToStringsMap[type]) {
        if (std::find(variants.begin(), variants.end(), rawName) != variants.end()) {
            return id;
        }
    }

    return ParameterID::UNKNOWN;
}

void MaterialParameterMapper::addParameterVariants(ParameterID id, MaterialParameterTypes type, 
                                                   const std::vector<std::string>& variants) {

    std::vector<std::string> lowerCaseVariants;
    for (const auto& variant : variants) {
        lowerCaseVariants.push_back(toLowerCase(variant));
    }
    
    m_typeToIDsToStringsMap[type][id] = lowerCaseVariants;

}






std::string MaterialParameterMapper::toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}


// Helper Functions

std::string parameterTypeToString(MaterialParameterTypes type) {
    switch (type) {
        case MaterialParameterTypes::FLOAT: return "float";
        case MaterialParameterTypes::INT: return "int";
        case MaterialParameterTypes::UINT: return "uint";
        case MaterialParameterTypes::BOOL: return "bool";
        case MaterialParameterTypes::VEC2: return "vec2";
        case MaterialParameterTypes::VEC3: return "vec3";
        case MaterialParameterTypes::VEC4: return "vec4";
        case MaterialParameterTypes::MAT3: return "mat3";
        case MaterialParameterTypes::MAT4: return "mat4";
        case MaterialParameterTypes::TEXTURE_2D: return "texture2D";
        case MaterialParameterTypes::TEXTURE_CUBE: return "textureCube";
        case MaterialParameterTypes::TEXTURE_3D: return "texture3D";
        case MaterialParameterTypes::SAMPLER: return "sampler";
        default: return "unknown";
    }
}

std::string parameterIdToString(ParameterID id) {
    switch (id) {
        case ParameterID::ALBEDO: return "ALBEDO";
        case ParameterID::ALBEDO_MAP: return "ALBEDO_MAP";
        case ParameterID::METALLIC: return "METALLIC";
        case ParameterID::METALLIC_MAP: return "METALLIC_MAP";
        case ParameterID::ROUGHNESS: return "ROUGHNESS";
        case ParameterID::ROUGHNESS_MAP: return "ROUGHNESS_MAP";
        case ParameterID::NORMAL: return "NORMAL";
        case ParameterID::NORMAL_MAP: return "NORMAL_MAP";
        case ParameterID::HEIGHT_MAP: return "HEIGHT_MAP";
        case ParameterID::AO_MAP: return "AO_MAP";
        case ParameterID::EMISSIVE: return "EMISSIVE";
        case ParameterID::EMISSIVE_MAP: return "EMISSIVE_MAP";
        case ParameterID::SPECULAR: return "SPECULAR";
        case ParameterID::SPECULAR_MAP: return "SPECULAR_MAP";
        default: return "UNKNOWN";
    }
}

MaterialParameterTypes stringToMaterialParameterType(const std::string& str) {
    if (str == "float") return MaterialParameterTypes::FLOAT;
    if (str == "int") return MaterialParameterTypes::INT;
    if (str == "uint") return MaterialParameterTypes::UINT;
    if (str == "bool") return MaterialParameterTypes::BOOL;
    if (str == "vec2") return MaterialParameterTypes::VEC2;
    if (str == "vec3") return MaterialParameterTypes::VEC3; 
    if (str == "vec4") return MaterialParameterTypes::VEC4;
    if (str == "mat3") return MaterialParameterTypes::MAT3;
    if (str == "mat4") return MaterialParameterTypes::MAT4;
    if (str == "texture2D") return MaterialParameterTypes::TEXTURE_2D;
    if (str == "textureCube") return MaterialParameterTypes::TEXTURE_CUBE;
    if (str == "texture3D") return MaterialParameterTypes::TEXTURE_3D;
    if (str == "sampler") return MaterialParameterTypes::SAMPLER;
    return MaterialParameterTypes::UNKNOWN;
}


MaterialParameter::MaterialParameter(const DescriptorParamInfo &info)
{
    m_info.parameterId = MaterialParameterMapper::getParameterID(info.name, stringToMaterialParameterType(info.type));
    m_info.type = stringToMaterialParameterType(info.type);
    m_info.parameterName = info.name;
    m_info.size = info.size;
    m_info.offset = info.offset;

    // Initialize the variant with the appropriate type
    switch (m_info.type) {
        case MaterialParameterTypes::FLOAT:
            m_value = 1.0f;
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
            m_value = glm::vec3(1.0f, 0.0f, 1.0f);
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
        default:
            m_value = std::monostate{};
            break;
    }
}




} // namespace Rapture