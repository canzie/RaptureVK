#ifndef RAPTURE__MATERIAL_GRAPH_TYPES_H
#define RAPTURE__MATERIAL_GRAPH_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Rapture {

enum class PinType {
    FLOAT,
    INT,
    VEC2,
    VEC3,
    VEC4,
    TEXTURE, // a bindless texture index
};

enum class GraphNodeType {
    NONE,

    POSITION,
    NORMAL,
    TANGENT,
    BITANGENT,
    TEXCOORD,

    CONSTANT_FLOAT,
    CONSTANT_INT,
    CONSTANT_VEC2,
    CONSTANT_VEC3,
    CONSTANT_VEC4,

    TEXTURE_SAMPLE,

    ADD_FLOAT,
    ADD_VEC3,
    ADD_INT,

    SUBTRACT_FLOAT,
    SUBTRACT_VEC3,
    SUBTRACT_INT,

    MULTIPLY_FLOAT,
    MULTIPLY_VEC3,
    MULTIPLY_INT,

    DIVIDE_FLOAT,
    DIVIDE_VEC3,
    DIVIDE_INT,

    ABS_FLOAT,
    ABS_VEC3,
    ABS_INT,

    MIN_FLOAT,
    MIN_VEC3,
    MIN_INT,

    MAX_FLOAT,
    MAX_VEC3,
    MAX_INT,

    CLAMP_FLOAT,
    CLAMP_VEC3,
    CLAMP_INT,

    SATURATE_FLOAT,
    SATURATE_VEC3,

    MIX_FLOAT,
    MIX_VEC3,

    STEP_FLOAT,
    STEP_VEC3,

    SMOOTHSTEP_FLOAT,
    SMOOTHSTEP_VEC3,

    FRACT_FLOAT,
    FRACT_VEC3,

    POWER_FLOAT,
    POWER_VEC3,

    SQRT_FLOAT,
    SQRT_VEC3,

    SIN_FLOAT,
    SIN_VEC3,

    COS_FLOAT,
    COS_VEC3,

    DOT_VEC3,
    CROSS_VEC3,
    NORMALIZE_VEC3,
    LENGTH_VEC3,
    DISTANCE_VEC3,

    COMBINE_VEC2,
    COMBINE_VEC3,
    COMBINE_VEC4,

    SPLIT_VEC2,
    SPLIT_VEC3,
    SPLIT_VEC4,

    NORMAL_MAP,
    NORMAL_MAP_RG,
    LUMINANCE,
    REMAP_FLOAT,

    SURFACE_OUTPUT,
};

/**
 * @brief A pin default stored as whichever scalar or vector type the pin uses
 *
 * The widest member overlaps the narrower ones, so a scalar default splats across the
 * vector components and the type read back matches the pin type.
 */
union PinValue {
    float f;
    int32_t i;
    glm::vec2 v2;
    glm::vec3 v3;
    glm::vec4 v4;

    constexpr PinValue() : v4(0.0f) {}
    constexpr PinValue(float value) : v4(value) {}
    constexpr PinValue(int value) : i(value) {}
    constexpr PinValue(glm::vec2 value) : v2(value) {}
    constexpr PinValue(glm::vec3 value) : v3(value) {}
    constexpr PinValue(glm::vec4 value) : v4(value) {}
};

struct PinDef {
    std::string name; // referenced in a template as {name}
    PinType type;
    PinValue defaultValue{};       // emitted as a literal when an input pin is unconnected
    std::string glslTemplate = {}; // this output pin's expression on a multi output node
};

/**
 * @brief Data-driven description of a node type: pins plus a GLSL expression template
 *
 * The compiler substitutes placeholders into glslTemplate rather than switching on the
 * type, so adding a node type is a data entry, not a compiler edit.
 */
struct NodeDefinition {
    GraphNodeType type = GraphNodeType::NONE;
    std::vector<PinDef> inputs = {};
    std::vector<PinDef> outputs = {};
    std::string glslTemplate = {}; // single output nodes: the RHS expression
};

/**
 * @brief GLSL scalar/vector type name for a pin type
 * @param type The pin type
 * @return The matching GLSL type keyword ("float", "vec2", "vec3", "vec4")
 */
const char *graph_pinTypeGlsl(PinType type);

/**
 * @brief Number of components in a pin type
 * @param type The pin type
 * @return The component count, 1 to 4
 */
uint32_t graph_pinTypeComponents(PinType type);

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_TYPES_H
