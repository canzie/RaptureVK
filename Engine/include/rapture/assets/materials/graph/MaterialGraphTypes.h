#ifndef RAPTURE__MATERIAL_GRAPH_TYPES_H
#define RAPTURE__MATERIAL_GRAPH_TYPES_H

#include <cstdint>
#include <string>
#include <string_view>
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
    TEXTURE_WHITE_NOISE,
    TEXTURE_PERLIN,
    TEXTURE_SIMPLEX,
    TEXTURE_RIDGED,

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

    SLOPE,
    FACING_ANGLE,
    TRIPLANAR_SAMPLE,
    TRIPLANAR_NORMAL,
    HEIGHT_BLEND_WEIGHT,

    TERRAIN_HEIGHT,
    TERRAIN_CURVATURE,
    TERRAIN_LOD,
    TERRAIN_EROSION,
    TERRAIN_CONTINENTALNESS,
    TERRAIN_PEAKS_VALLEYS,

    SURFACE_OUTPUT,
};

#define RP_GRAPH_NODE_TYPES(X)                                                                                                    \
    X(NONE)                                                                                                                       \
    X(POSITION)                                                                                                                   \
    X(NORMAL) X(TANGENT) X(BITANGENT) X(TEXCOORD) X(CONSTANT_FLOAT) X(CONSTANT_INT) X(CONSTANT_VEC2) X(CONSTANT_VEC3)             \
        X(CONSTANT_VEC4) X(TEXTURE_SAMPLE) X(TEXTURE_WHITE_NOISE) X(TEXTURE_PERLIN) X(TEXTURE_SIMPLEX) X(TEXTURE_RIDGED)          \
            X(ADD_FLOAT) X(ADD_VEC3) X(ADD_INT) X(SUBTRACT_FLOAT) X(SUBTRACT_VEC3) X(SUBTRACT_INT) X(MULTIPLY_FLOAT)              \
                X(MULTIPLY_VEC3) X(MULTIPLY_INT) X(DIVIDE_FLOAT) X(DIVIDE_VEC3) X(DIVIDE_INT) X(ABS_FLOAT) X(ABS_VEC3) X(ABS_INT) \
                    X(MIN_FLOAT) X(MIN_VEC3) X(MIN_INT) X(MAX_FLOAT) X(MAX_VEC3) X(MAX_INT) X(CLAMP_FLOAT) X(CLAMP_VEC3)          \
                        X(CLAMP_INT) X(SATURATE_FLOAT) X(SATURATE_VEC3) X(MIX_FLOAT) X(MIX_VEC3) X(STEP_FLOAT) X(STEP_VEC3)       \
                            X(SMOOTHSTEP_FLOAT) X(SMOOTHSTEP_VEC3) X(FRACT_FLOAT) X(FRACT_VEC3) X(POWER_FLOAT) X(POWER_VEC3)      \
                                X(SQRT_FLOAT) X(SQRT_VEC3) X(SIN_FLOAT) X(SIN_VEC3) X(COS_FLOAT) X(COS_VEC3) X(DOT_VEC3)          \
                                    X(CROSS_VEC3) X(NORMALIZE_VEC3) X(LENGTH_VEC3) X(DISTANCE_VEC3) X(COMBINE_VEC2)               \
                                        X(COMBINE_VEC3) X(COMBINE_VEC4) X(SPLIT_VEC2) X(SPLIT_VEC3) X(SPLIT_VEC4) X(NORMAL_MAP)   \
                                            X(NORMAL_MAP_RG) X(LUMINANCE) X(REMAP_FLOAT) X(SLOPE) X(FACING_ANGLE)                 \
                                                X(TRIPLANAR_SAMPLE) X(TRIPLANAR_NORMAL) X(HEIGHT_BLEND_WEIGHT) X(TERRAIN_HEIGHT)  \
                                                    X(TERRAIN_CURVATURE) X(TERRAIN_LOD) X(TERRAIN_EROSION)                        \
                                                        X(TERRAIN_CONTINENTALNESS) X(TERRAIN_PEAKS_VALLEYS) X(SURFACE_OUTPUT)

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
 * type, so adding a node type is a data entry, not a compiler edit. A template references an
 * input pin as {pinName} and a domain input as {$inputName}.
 */
struct NodeDefinition {
    GraphNodeType type = GraphNodeType::NONE;
    std::vector<PinDef> inputs = {};
    std::vector<PinDef> outputs = {};
    std::string glslTemplate = {}; // single output nodes: the RHS expression

    // Domain inputs the templates reference, scanned from them by NodeRegistry::registerNode.
    // A node is usable in a domain only if the domain provides every name here.
    std::vector<std::string> requiredInputs = {};
};

/**
 * @brief Collect the {$name} domain input references in a template
 * @param templateStr The GLSL template to scan
 * @param out Appended with each referenced name, skipping ones already present
 */
void Graph_scanRequiredInputs(std::string_view templateStr, std::vector<std::string> &out);

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

/**
 * @brief Stable serialization name for a node type
 * @param type The node type
 * @return The enum spelling of the type, "NONE" for an unknown value
 */
const char *Graph_nodeTypeName(GraphNodeType type);

/**
 * @brief Resolve a node type from its serialization name
 * @param name The enum spelling to look up
 * @return The matching type, or GraphNodeType::NONE when the name is unknown
 */
GraphNodeType Graph_nodeTypeFromName(std::string_view name);

} // namespace Rapture

#endif // RAPTURE__MATERIAL_GRAPH_TYPES_H
