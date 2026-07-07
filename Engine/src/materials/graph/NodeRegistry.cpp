#include "NodeRegistry.h"

#include <unordered_map>
#include <unordered_set>

#include "logging/Log.h"
#include "utils/rp_assert.h"

namespace Rapture {

static std::unordered_map<GraphNodeType, NodeDefinition> s_definitions;
static bool s_builtinsRegistered = false;

/**
 * @brief Assert a node definition uses no reserved or duplicate pin names
 *
 * Pin names become {name} template placeholders, so they must be unique within a direction and
 * must not collide with the built in {tex} / {const} placeholders.
 */
static void s_validateDefinition(const NodeDefinition &def)
{
    auto checkPins = [&](const std::vector<PinDef> &pins, const char *direction) {
        std::unordered_set<std::string> names;
        for (const auto &pin : pins) {
            bool reserved = pin.name == "tex" || pin.name == "const";
            if (reserved) {
                RP_CORE_ERROR("Node type {} has a pin using the reserved template name '{}'",
                              static_cast<int>(def.type), pin.name);
            }
            RP_ASSERT(!reserved, "node pin uses a reserved template name (tex or const)");

            bool unique = names.insert(pin.name).second;
            if (!unique) {
                RP_CORE_ERROR("Node type {} has a duplicate {} pin name '{}'", static_cast<int>(def.type), direction,
                              pin.name);
            }
            RP_ASSERT(unique, "node has a duplicate pin name within one direction");
        }
    };
    checkPins(def.inputs, "input");
    checkPins(def.outputs, "output");
}

const char *graph_pinTypeGlsl(PinType type)
{
    switch (type) {
    case PinType::FLOAT:
        return "float";
    case PinType::INT:
        return "int";
    case PinType::VEC2:
        return "vec2";
    case PinType::VEC3:
        return "vec3";
    case PinType::VEC4:
        return "vec4";
    }
    return "vec4";
}

uint32_t graph_pinTypeComponents(PinType type)
{
    switch (type) {
    case PinType::FLOAT:
    case PinType::INT:
        return 1;
    case PinType::VEC2:
        return 2;
    case PinType::VEC3:
        return 3;
    case PinType::VEC4:
        return 4;
    }
    return 4;
}

const NodeDefinition *NodeRegistry::get(GraphNodeType type)
{
    auto it = s_definitions.find(type);
    if (it == s_definitions.end()) return nullptr;
    return &it->second;
}

void NodeRegistry::registerNode(NodeDefinition def)
{
    s_validateDefinition(def);
    GraphNodeType key = def.type;
    s_definitions[key] = std::move(def);
}

void NodeRegistry::registerBuiltins()
{
    if (s_builtinsRegistered) return;
    s_builtinsRegistered = true;

    // Input readers
    registerNode({.type = GraphNodeType::TEXCOORD, .outputs = {{"out", PinType::VEC2}}, .glslTemplate = "si.uv"});
    registerNode({.type = GraphNodeType::POSITION, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.worldPos"});
    registerNode({.type = GraphNodeType::NORMAL, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.worldNormal"});
    registerNode({.type = GraphNodeType::TANGENT, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.tangent"});
    registerNode({.type = GraphNodeType::BITANGENT, .outputs = {{"out", PinType::VEC3}}, .glslTemplate = "si.bitangent"});

    // Slot-backed values, {const} expands to the packed slice value read at the node's type
    registerNode({.type = GraphNodeType::CONSTANT_FLOAT,
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_INT,
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_VEC2,
                  .outputs = {{"out", PinType::VEC2}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_VEC3,
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});
    registerNode({.type = GraphNodeType::CONSTANT_VEC4,
                  .outputs = {{"out", PinType::VEC4}},
                  .glslTemplate = "{const}",
                  .resourceKind = ResourceKind::CONSTANT});

    // Texture sample
    registerNode({.type = GraphNodeType::TEXTURE_SAMPLE,
                  .inputs = {{"uv", PinType::VEC2}},
                  .outputs = {{"out", PinType::VEC4}},
                  .glslTemplate = "texture(u_textures[nonuniformEXT({tex})], {uv})",
                  .resourceKind = ResourceKind::TEXTURE});

    // Add
    registerNode({.type = GraphNodeType::ADD_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}, {"b", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{a} + {b}"});
    registerNode({.type = GraphNodeType::ADD_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} + {b}"});
    registerNode({.type = GraphNodeType::ADD_INT,
                  .inputs = {{"a", PinType::INT}, {"b", PinType::INT}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} + {b}"});

    // Subtract
    registerNode({.type = GraphNodeType::SUBTRACT_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}, {"b", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{a} - {b}"});
    registerNode({.type = GraphNodeType::SUBTRACT_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} - {b}"});
    registerNode({.type = GraphNodeType::SUBTRACT_INT,
                  .inputs = {{"a", PinType::INT}, {"b", PinType::INT}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} - {b}"});

    // Multiply
    registerNode({.type = GraphNodeType::MULTIPLY_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, 1.0f}, {"b", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{a} * {b}"});
    registerNode({.type = GraphNodeType::MULTIPLY_VEC3,
                  .inputs = {{"a", PinType::VEC3, 1.0f}, {"b", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} * {b}"});
    registerNode({.type = GraphNodeType::MULTIPLY_INT,
                  .inputs = {{"a", PinType::INT, 1}, {"b", PinType::INT, 1}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} * {b}"});

    // Divide
    registerNode({.type = GraphNodeType::DIVIDE_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, 1.0f}, {"b", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "{a} / {b}"});
    registerNode({.type = GraphNodeType::DIVIDE_VEC3,
                  .inputs = {{"a", PinType::VEC3, 1.0f}, {"b", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "{a} / {b}"});
    registerNode({.type = GraphNodeType::DIVIDE_INT,
                  .inputs = {{"a", PinType::INT, 1}, {"b", PinType::INT, 1}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "{a} / {b}"});

    // Abs
    registerNode({.type = GraphNodeType::ABS_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "abs({a})"});
    registerNode({.type = GraphNodeType::ABS_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "abs({a})"});
    registerNode({.type = GraphNodeType::ABS_INT,
                  .inputs = {{"a", PinType::INT}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "abs({a})"});

    // Min
    registerNode({.type = GraphNodeType::MIN_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}, {"b", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "min({a}, {b})"});
    registerNode({.type = GraphNodeType::MIN_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "min({a}, {b})"});
    registerNode({.type = GraphNodeType::MIN_INT,
                  .inputs = {{"a", PinType::INT}, {"b", PinType::INT}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "min({a}, {b})"});

    // Max
    registerNode({.type = GraphNodeType::MAX_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}, {"b", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "max({a}, {b})"});
    registerNode({.type = GraphNodeType::MAX_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "max({a}, {b})"});
    registerNode({.type = GraphNodeType::MAX_INT,
                  .inputs = {{"a", PinType::INT}, {"b", PinType::INT}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "max({a}, {b})"});

    // Clamp, min and max default to the 0..1 range
    registerNode({.type = GraphNodeType::CLAMP_FLOAT,
                  .inputs = {{"x", PinType::FLOAT}, {"min", PinType::FLOAT}, {"max", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "clamp({x}, {min}, {max})"});
    registerNode({.type = GraphNodeType::CLAMP_VEC3,
                  .inputs = {{"x", PinType::VEC3}, {"min", PinType::VEC3}, {"max", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "clamp({x}, {min}, {max})"});
    registerNode({.type = GraphNodeType::CLAMP_INT,
                  .inputs = {{"x", PinType::INT}, {"min", PinType::INT}, {"max", PinType::INT, 1}},
                  .outputs = {{"out", PinType::INT}},
                  .glslTemplate = "clamp({x}, {min}, {max})"});

    // Saturate, clamp to 0..1 with bounds built at the pin type
    registerNode({.type = GraphNodeType::SATURATE_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "clamp({a}, float(0.0), float(1.0))"});
    registerNode({.type = GraphNodeType::SATURATE_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "clamp({a}, vec3(0.0), vec3(1.0))"});

    // Mix, t is always scalar
    registerNode({.type = GraphNodeType::MIX_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}, {"b", PinType::FLOAT, 1.0f}, {"t", PinType::FLOAT, 0.5f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "mix({a}, {b}, {t})"});
    registerNode({.type = GraphNodeType::MIX_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3, 1.0f}, {"t", PinType::FLOAT, 0.5f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "mix({a}, {b}, {t})"});

    // Step
    registerNode({.type = GraphNodeType::STEP_FLOAT,
                  .inputs = {{"edge", PinType::FLOAT, 0.5f}, {"x", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "step({edge}, {x})"});
    registerNode({.type = GraphNodeType::STEP_VEC3,
                  .inputs = {{"edge", PinType::VEC3, 0.5f}, {"x", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "step({edge}, {x})"});

    // Smoothstep
    registerNode({.type = GraphNodeType::SMOOTHSTEP_FLOAT,
                  .inputs = {{"edge0", PinType::FLOAT}, {"edge1", PinType::FLOAT, 1.0f}, {"x", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "smoothstep({edge0}, {edge1}, {x})"});
    registerNode({.type = GraphNodeType::SMOOTHSTEP_VEC3,
                  .inputs = {{"edge0", PinType::VEC3}, {"edge1", PinType::VEC3, 1.0f}, {"x", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "smoothstep({edge0}, {edge1}, {x})"});

    // Fract
    registerNode({.type = GraphNodeType::FRACT_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "fract({a})"});
    registerNode({.type = GraphNodeType::FRACT_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "fract({a})"});

    // Power
    registerNode({.type = GraphNodeType::POWER_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, 1.0f}, {"b", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "pow({a}, {b})"});
    registerNode({.type = GraphNodeType::POWER_VEC3,
                  .inputs = {{"a", PinType::VEC3, 1.0f}, {"b", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "pow({a}, {b})"});

    // Sqrt
    registerNode({.type = GraphNodeType::SQRT_FLOAT,
                  .inputs = {{"a", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "sqrt({a})"});
    registerNode({.type = GraphNodeType::SQRT_VEC3,
                  .inputs = {{"a", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "sqrt({a})"});

    // Sin
    registerNode({.type = GraphNodeType::SIN_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "sin({a})"});
    registerNode({.type = GraphNodeType::SIN_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "sin({a})"});

    // Cos
    registerNode({.type = GraphNodeType::COS_FLOAT,
                  .inputs = {{"a", PinType::FLOAT}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "cos({a})"});
    registerNode({.type = GraphNodeType::COS_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "cos({a})"});

    // Dot
    registerNode({.type = GraphNodeType::DOT_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "dot({a}, {b})"});

    // Cross
    registerNode({.type = GraphNodeType::CROSS_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "cross({a}, {b})"});

    // Normalize
    registerNode({.type = GraphNodeType::NORMALIZE_VEC3,
                  .inputs = {{"a", PinType::VEC3, glm::vec3(0.0f, 0.0f, 1.0f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "normalize({a})"});

    // Length
    registerNode({.type = GraphNodeType::LENGTH_VEC3,
                  .inputs = {{"a", PinType::VEC3}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "length({a})"});

    // Distance
    registerNode({.type = GraphNodeType::DISTANCE_VEC3,
                  .inputs = {{"a", PinType::VEC3}, {"b", PinType::VEC3}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "distance({a}, {b})"});

    // Combine, scalars packed into a vector
    registerNode({.type = GraphNodeType::COMBINE_VEC2,
                  .inputs = {{"x", PinType::FLOAT}, {"y", PinType::FLOAT}},
                  .outputs = {{"out", PinType::VEC2}},
                  .glslTemplate = "vec2({x}, {y})"});
    registerNode({.type = GraphNodeType::COMBINE_VEC3,
                  .inputs = {{"x", PinType::FLOAT}, {"y", PinType::FLOAT}, {"z", PinType::FLOAT}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "vec3({x}, {y}, {z})"});
    registerNode({.type = GraphNodeType::COMBINE_VEC4,
                  .inputs = {{"x", PinType::FLOAT}, {"y", PinType::FLOAT}, {"z", PinType::FLOAT}, {"w", PinType::FLOAT}},
                  .outputs = {{"out", PinType::VEC4}},
                  .glslTemplate = "vec4({x}, {y}, {z}, {w})"});

    // Split, one output per component, each carries its own expression
    registerNode({.type = GraphNodeType::SPLIT_VEC2,
                  .inputs = {{"in", PinType::VEC2}},
                  .outputs = {{.name = "x", .type = PinType::FLOAT, .glslTemplate = "({in}).x"},
                              {.name = "y", .type = PinType::FLOAT, .glslTemplate = "({in}).y"}}});
    registerNode({.type = GraphNodeType::SPLIT_VEC3,
                  .inputs = {{"in", PinType::VEC3}},
                  .outputs = {{.name = "x", .type = PinType::FLOAT, .glslTemplate = "({in}).x"},
                              {.name = "y", .type = PinType::FLOAT, .glslTemplate = "({in}).y"},
                              {.name = "z", .type = PinType::FLOAT, .glslTemplate = "({in}).z"}}});
    registerNode({.type = GraphNodeType::SPLIT_VEC4,
                  .inputs = {{"in", PinType::VEC4}},
                  .outputs = {{.name = "x", .type = PinType::FLOAT, .glslTemplate = "({in}).x"},
                              {.name = "y", .type = PinType::FLOAT, .glslTemplate = "({in}).y"},
                              {.name = "z", .type = PinType::FLOAT, .glslTemplate = "({in}).z"},
                              {.name = "w", .type = PinType::FLOAT, .glslTemplate = "({in}).w"}}});

    // Normal map, unpacks a tangent space sample and rotates it into world space
    registerNode({.type = GraphNodeType::NORMAL_MAP,
                  .inputs = {{"sample", PinType::VEC3, glm::vec3(0.5f, 0.5f, 1.0f)}},
                  .outputs = {{"out", PinType::VEC3}},
                  .glslTemplate = "normalize(mat3(normalize(si.tangent), normalize(si.bitangent), normalize(si.worldNormal)) * ({sample} * 2.0 - 1.0))"});

    // Luminance
    registerNode({.type = GraphNodeType::LUMINANCE,
                  .inputs = {{"color", PinType::VEC3, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "dot({color}, vec3(0.2126, 0.7152, 0.0722))"});

    // Remap from an input range to an output range
    registerNode({.type = GraphNodeType::REMAP_FLOAT,
                  .inputs = {{"x", PinType::FLOAT},
                             {"inMin", PinType::FLOAT},
                             {"inMax", PinType::FLOAT, 1.0f},
                             {"outMin", PinType::FLOAT},
                             {"outMax", PinType::FLOAT, 1.0f}},
                  .outputs = {{"out", PinType::FLOAT}},
                  .glslTemplate = "({outMin} + ({outMax} - {outMin}) * (({x} - {inMin}) / ({inMax} - {inMin})))"});

    // Sink
    registerNode({.type = GraphNodeType::SURFACE_OUTPUT,
                  .inputs = {{"albedo", PinType::VEC3},
                             {"normal", PinType::VEC3},
                             {"roughness", PinType::FLOAT},
                             {"metallic", PinType::FLOAT},
                             {"ao", PinType::FLOAT}}});
}

} // namespace Rapture
