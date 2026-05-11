#ifndef RAPTURE__MATERIAL_PARAMETERS_H
#define RAPTURE__MATERIAL_PARAMETERS_H

#include "MaterialData.h"
#include <cstddef>
#include <string_view>

namespace Rapture {

enum class ParameterID {
    ALBEDO,
    ROUGHNESS,
    METALLIC,
    AO,
    EMISSIVE,
    ALPHA,

    ALBEDO_MAP,
    NORMAL_MAP,
    METALLIC_ROUGHNESS_MAP,
    AO_MAP,
    EMISSIVE_MAP,
    HEIGHT_MAP,
    SPECULAR_MAP,

    TILING_SCALE,
    HEIGHT_BLEND,
    SLOPE_THRESHOLD,
    SPLAT_MAP
};

enum class ParamType {
    FLOAT,
    VEC3,
    VEC4,
    TEXTURE
};

struct ParamInfo {
    ParameterID id;
    ParamType type;
    uint32_t flag;
    size_t offset;
    size_t size;
    std::string_view name;
};

// clang-format off
constexpr ParamInfo PARAM_REGISTRY[] = {
    {ParameterID::ALBEDO,      ParamType::VEC4,  0, offsetof(MaterialData, albedo),      sizeof(glm::vec4), "albedo"},
    {ParameterID::ROUGHNESS,   ParamType::FLOAT, 0, offsetof(MaterialData, roughness),   sizeof(float),     "roughness"},
    {ParameterID::METALLIC,    ParamType::FLOAT, 0, offsetof(MaterialData, metallic),    sizeof(float),     "metallic"},
    {ParameterID::AO,          ParamType::FLOAT, 0, offsetof(MaterialData, ao),          sizeof(float),     "ao"},
    {ParameterID::EMISSIVE,    ParamType::VEC4,  0, offsetof(MaterialData, emissive),    sizeof(glm::vec4), "emissive"},
    {ParameterID::ALPHA,       ParamType::FLOAT, 0, offsetof(MaterialData, albedo) + 12, sizeof(float),     "alpha"},

    {ParameterID::ALBEDO_MAP,             ParamType::TEXTURE, MAT_FLAG_HAS_ALBEDO_MAP,             offsetof(MaterialData, texIndices0),      sizeof(uint32_t), "albedoMap"},
    {ParameterID::NORMAL_MAP,             ParamType::TEXTURE, MAT_FLAG_HAS_NORMAL_MAP,             offsetof(MaterialData, texIndices0) + 4,  sizeof(uint32_t), "normalMap"},
    {ParameterID::METALLIC_ROUGHNESS_MAP, ParamType::TEXTURE, MAT_FLAG_HAS_METALLIC_ROUGHNESS_MAP, offsetof(MaterialData, texIndices0) + 8,  sizeof(uint32_t), "metallicRoughnessMap"},
    {ParameterID::AO_MAP,                 ParamType::TEXTURE, MAT_FLAG_HAS_AO_MAP,                 offsetof(MaterialData, texIndices0) + 12, sizeof(uint32_t), "aoMap"},
    {ParameterID::EMISSIVE_MAP,           ParamType::TEXTURE, MAT_FLAG_HAS_EMISSIVE_MAP,           offsetof(MaterialData, texIndices1),      sizeof(uint32_t), "emissiveMap"},
    {ParameterID::HEIGHT_MAP,             ParamType::TEXTURE, MAT_FLAG_HAS_HEIGHT_MAP,             offsetof(MaterialData, texIndices1) + 4,  sizeof(uint32_t), "heightMap"},
    {ParameterID::SPECULAR_MAP,           ParamType::TEXTURE, MAT_FLAG_HAS_SPECULAR_MAP,           offsetof(MaterialData, texIndices1) + 8,  sizeof(uint32_t), "specularMap"},

    {ParameterID::TILING_SCALE,    ParamType::FLOAT,   0,                      offsetof(MaterialData, tilingScale),      sizeof(float),    "tilingScale"},
    {ParameterID::HEIGHT_BLEND,    ParamType::FLOAT,   0,                      offsetof(MaterialData, heightBlend),      sizeof(float),    "heightBlend"},
    {ParameterID::SLOPE_THRESHOLD, ParamType::FLOAT,   0,                      offsetof(MaterialData, slopeThreshold),   sizeof(float),    "slopeThreshold"},
    {ParameterID::SPLAT_MAP,       ParamType::TEXTURE, MAT_FLAG_HAS_SPLAT_MAP, offsetof(MaterialData, texIndices1) + 12, sizeof(uint32_t), "splatMap"},
};
// clang-format on

constexpr size_t PARAM_COUNT = sizeof(PARAM_REGISTRY) / sizeof(PARAM_REGISTRY[0]);

inline const ParamInfo *getParamInfo(ParameterID id)
{
    for (size_t i = 0; i < PARAM_COUNT; ++i) {
        if (PARAM_REGISTRY[i].id == id) {
            return &PARAM_REGISTRY[i];
        }
    }
    return nullptr;
}

inline bool isTextureParam(ParameterID id)
{
    const ParamInfo *info = getParamInfo(id);
    return info && info->type == ParamType::TEXTURE;
}

std::string_view parameterIdToString(ParameterID id);

} // namespace Rapture

#endif // RAPTURE__MATERIAL_PARAMETERS_H
