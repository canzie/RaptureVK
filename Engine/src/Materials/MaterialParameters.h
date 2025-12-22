#pragma once

#include "Shaders/ShaderReflections.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Logging/Log.h"

#include <variant>

#include "glm/glm.hpp"

#include "Textures/Texture.h"

namespace Rapture {

enum class ParameterID : uint16_t {
    // Base color/albedo parameters
    ALBEDO,
    ALBEDO_MAP,

    // Metallic workflow parameters
    METALLIC,
    METALLIC_MAP,

    // Roughness parameters
    ROUGHNESS,
    ROUGHNESS_MAP,

    METALLIC_ROUGHNESS_MAP,

    // Normal mapping
    NORMAL,
    NORMAL_MAP,

    // Additional texture maps
    HEIGHT_MAP,
    AO_MAP,
    AO,

    // Extended PBR parameters
    EMISSIVE,
    EMISSIVE_MAP,

    SPECULAR,
    SPECULAR_MAP,

    UNKNOWN = 0xFFFF
};

enum class MaterialParameterTypes {
    FLOAT = 1,
    INT = 2,
    UINT = 4,
    UINT64 = 8,
    BOOL = 16,
    VEC2 = 32,
    VEC3 = 64,
    VEC4 = 128,
    UVEC2 = 256,
    UVEC3 = 512,
    UVEC4 = 1024,
    IVEC2 = 2048,
    IVEC3 = 4096,
    IVEC4 = 8192,
    MAT3 = 16384,
    MAT4 = 32768,
    TEXTURE = 65536,
    COMBINED_IMAGE_SAMPLER = 131072,
    SAMPLER = 262144,

    UNKNOWN = 0
};

// Helper functions for bitwise operations on MaterialParameterTypes
inline MaterialParameterTypes operator|(MaterialParameterTypes a, MaterialParameterTypes b)
{
    return static_cast<MaterialParameterTypes>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline MaterialParameterTypes operator&(MaterialParameterTypes a, MaterialParameterTypes b)
{
    return static_cast<MaterialParameterTypes>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasType(MaterialParameterTypes combined, MaterialParameterTypes type)
{
    return (combined & type) == type;
}

// Extended parameter information
struct MaterialParameterInfo {
    ParameterID parameterId;
    MaterialParameterTypes type;
    std::string parameterName;
    uint32_t binding = 0;
    uint32_t offset = 0; // For uniform buffer offsets
    uint32_t size = 0;   // Size in bytes
    bool isTexture = false;
    bool isUniform = false;
    bool isPushConstant = false;
};

MaterialParameterTypes stringToMaterialParameterType(const std::string &str);
std::string parameterTypeToString(MaterialParameterTypes type);
std::string parameterIdToString(ParameterID id);

using MaterialTypes = std::variant<std::monostate, int32_t, uint32_t, uint64_t, bool, float, glm::vec2, glm::vec3, glm::vec4,
                                   glm::mat3, glm::mat4, std::shared_ptr<Texture>>;

class MaterialParameter {
  public:
    MaterialParameter() : m_value(std::monostate{})
    {
        m_info.parameterId = ParameterID::UNKNOWN;
        m_info.type = MaterialParameterTypes::UNKNOWN;
        m_info.parameterName = "";
        // Other members of m_info (binding, offset, size, bool flags)
        // will use their default member initializers if present, or be zero/false.
    }

    MaterialParameter(const DescriptorParamInfo &info, uint32_t binding = 0);

    float asFloat() { return std::get<float>(m_value); }
    int32_t asInt() { return std::get<int32_t>(m_value); }
    uint32_t asUInt() { 
        if (m_info.type == MaterialParameterTypes::UINT) {
            return std::get<uint32_t>(m_value);
        }
        return UINT32_MAX;
    }
    uint64_t asUInt64() { return std::get<uint64_t>(m_value); }
    bool asBool() { return std::get<bool>(m_value); }
    glm::vec2 asVec2() { return std::get<glm::vec2>(m_value); }
    glm::vec3 asVec3() { return std::get<glm::vec3>(m_value); }
    glm::vec4 asVec4() { return std::get<glm::vec4>(m_value); }
    glm::mat3 asMat3() { return std::get<glm::mat3>(m_value); }
    glm::mat4 asMat4() { return std::get<glm::mat4>(m_value); }
    std::shared_ptr<Texture> asTexture() { return std::get<std::shared_ptr<Texture>>(m_value); }

    void *asRaw()
    {
        switch (m_info.type) {
        case MaterialParameterTypes::FLOAT:
            return &std::get<float>(m_value);
        case MaterialParameterTypes::INT:
            return &std::get<int32_t>(m_value);
        case MaterialParameterTypes::UINT:
            return &std::get<uint32_t>(m_value);
        case MaterialParameterTypes::UINT64:
            return &std::get<uint64_t>(m_value);
        case MaterialParameterTypes::BOOL:
            return &std::get<bool>(m_value);
        case MaterialParameterTypes::VEC2:
            return &std::get<glm::vec2>(m_value);
        case MaterialParameterTypes::VEC3:
            return &std::get<glm::vec3>(m_value);
        case MaterialParameterTypes::VEC4:
            return &std::get<glm::vec4>(m_value);
        case MaterialParameterTypes::MAT3:
            return &std::get<glm::mat3>(m_value);
        case MaterialParameterTypes::MAT4:
            return &std::get<glm::mat4>(m_value);
        default:
            return nullptr;
        }
    }

    template <typename T> void setValue(const T &value)
    {
        // Assign the value to the variant.
        // If T is not one of the types in MaterialTypes, this will result in a compile-time error.
        // Runtime checks to ensure T aligns with m_info.type can be added if needed.
        m_value = value;
    }

  public:
    MaterialParameterInfo m_info;
    MaterialTypes m_value;
};

// Parameter name mappings for fuzzy string matching
class MaterialParameterMapper {
  public:
    static void initializeMappings();
    static ParameterID getParameterID(const std::string &rawName, MaterialParameterTypes type);

  private:
    static void addParameterVariants(ParameterID id, MaterialParameterTypes type, const std::vector<std::string> &variants);

    static std::string toLowerCase(const std::string &str);

  private:
    // leave me alone
    static std::unordered_map<MaterialParameterTypes, std::unordered_map<ParameterID, std::vector<std::string>>>
        m_typeToIDsToStringsMap;
    static bool m_isInitialized;
};

} // namespace Rapture
