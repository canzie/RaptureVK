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
};

enum class GraphNodeType {
    NONE,

    POSITION,
    NORMAL,
    TEXCOORD,

    CONSTANT_FLOAT,
    CONSTANT_INT,
    CONSTANT_VEC3,
    CONSTANT_VEC4,

    TEXTURE_SAMPLE,

    MULTIPLY_FLOAT,
    MULTIPLY_VEC3,
    MULTIPLY_INT,

    ADD_VEC3,
    ADD_INT,

    MIX_FLOAT,
    MIX_VEC3,

    FRACT_VEC3,

    SURFACE_OUTPUT,
};

/**
 * @brief Whether a node consumes a per-instance GraphInstanceData pool slot
 */
enum class ResourceKind {
    NONE,     // pure compute or input reader
    TEXTURE,  // reserves one textures[] slot, exposed to the template as {tex}
    CONSTANT, // reserves one constants[] slot, exposed to the template as {const}
};

struct PinDef {
    std::string name; // referenced in a template as {name}
    PinType type;
    glm::vec4 defaultValue{0.0f}; // emitted as a literal when the input pin is unconnected
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
    std::string glslTemplate = {}; // for a single output node this is the RHS expression, empty for a constant node
    ResourceKind resourceKind = ResourceKind::NONE;
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
