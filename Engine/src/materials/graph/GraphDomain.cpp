#include "GraphDomain.h"

#include <vector>

#include "NodeRegistry.h"
#include "logging/Log.h"
#include "utils/rp_assert.h"

namespace Rapture {

static std::vector<GraphDomain> s_domains;
static bool s_builtinsRegistered = false;

/**
 * @brief Assert every bound field of a domain maps to a sink pin of the same name and type
 *
 * The pass output fields and the sink node input pins are the two sides of the same channel set, so
 * a drift between them would silently misbind a value at codegen.
 */
static void s_validateDomain(const GraphDomain &domain)
{
    const NodeDefinition *sink = NodeRegistry::get(domain.sinkType);
    if (sink == nullptr) {
        RP_CORE_ERROR("Graph domain '{}' has no registered sink node definition", domain.name);
        RP_ASSERT(sink != nullptr, "graph domain sink node type is not registered");
        return;
    }

    for (const auto &pass : domain.passes) {
        for (const auto &field : pass.fields) {
            if (field.constant) {
                continue;
            }
            const PinDef *pin = nullptr;
            for (const auto &input : sink->inputs) {
                if (input.name == field.name) {
                    pin = &input;
                    break;
                }
            }
            if (pin == nullptr) {
                RP_CORE_ERROR("Graph domain '{}' pass '{}' binds field '{}' with no matching sink pin", domain.name,
                              pass.id, field.name);
                RP_ASSERT(pin != nullptr, "graph pass output field has no matching sink input pin");
                continue;
            }
            if (pin->type != field.type) {
                RP_CORE_ERROR("Graph domain '{}' pass '{}' field '{}' type disagrees with its sink pin", domain.name,
                              pass.id, field.name);
                RP_ASSERT(pin->type == field.type, "graph pass output field type disagrees with its sink pin");
            }
        }
    }
}

const GraphDomain *GraphDomainRegistry::forSink(GraphNodeType sinkType)
{
    for (const auto &domain : s_domains) {
        if (domain.sinkType == sinkType) {
            return &domain;
        }
    }
    return nullptr;
}

const std::vector<GraphDomain> &GraphDomainRegistry::all()
{
    return s_domains;
}

void GraphDomainRegistry::registerDomain(GraphDomain domain)
{
    s_validateDomain(domain);
    for (auto &existing : s_domains) {
        if (existing.sinkType == domain.sinkType) {
            existing = std::move(domain);
            return;
        }
    }
    s_domains.push_back(std::move(domain));
}

void GraphDomainRegistry::registerBuiltins()
{
    NodeRegistry::registerBuiltins();
    if (s_builtinsRegistered) {
        return;
    }
    s_builtinsRegistered = true;

    // The full G-buffer pass: every surface channel, plus the constant shading model id. albedo and
    // emission carry a debug errorFallback used only by the unknown-graph dispatcher case.
    GraphPass gbuffer;
    gbuffer.id = "gbuffer";
    gbuffer.structName = "SurfaceData";
    gbuffer.funcPrefix = "evalSurface_";
    gbuffer.dispatcherName = "evalSurfaceGraph";
    gbuffer.fileName = "SurfaceGraphs.glsl";
    gbuffer.guard = "SURFACE_GRAPHS_GLSL";
    gbuffer.fields = {
        {.name = "albedo", .type = PinType::VEC3, .fallback = "vec3(1.0)", .errorFallback = "vec3(1.0, 0.0, 1.0)"},
        {.name = "normal", .type = PinType::VEC3, .fallback = "normalize(si.worldNormal)"},
        {.name = "roughness", .type = PinType::FLOAT, .fallback = "0.5"},
        {.name = "metallic", .type = PinType::FLOAT, .fallback = "0.0"},
        {.name = "ao", .type = PinType::FLOAT, .fallback = "1.0"},
        {.name = "emission", .type = PinType::VEC4, .fallback = "vec4(1.0)", .errorFallback = "vec4(0.0)"},
        {.name = "emissiveStrength", .type = PinType::FLOAT, .fallback = "0.0"},
        {.name = "shadingModelId", .fallback = "SM_OPENPBR_STANDARD", .constant = true, .glslTypeOverride = "uint"},
    };

    // The reduced diffuse pass for RT/DDGI: albedo and emission only, mesh normal, no PBR channels.
    // Its smaller root set lets the compiler prune any node feeding only the dropped channels.
    GraphPass diffuse;
    diffuse.id = "diffuse";
    diffuse.structName = "SurfaceDataDiffuse";
    diffuse.funcPrefix = "evalSurfaceDiffuse_";
    diffuse.dispatcherName = "evalSurfaceGraphDiffuse";
    diffuse.fileName = "SurfaceGraphsDiffuse.glsl";
    diffuse.guard = "SURFACE_GRAPHS_DIFFUSE_GLSL";
    diffuse.fields = {
        {.name = "albedo", .type = PinType::VEC3, .fallback = "vec3(1.0)", .errorFallback = "vec3(1.0, 0.0, 1.0)"},
        {.name = "normal", .type = PinType::VEC3, .fallback = "normalize(si.worldNormal)"},
        {.name = "emission", .type = PinType::VEC4, .fallback = "vec4(1.0)", .errorFallback = "vec4(0.0)"},
        {.name = "emissiveStrength", .type = PinType::FLOAT, .fallback = "0.0"},
    };

    GraphDomain surface;
    surface.name = "Surface";
    surface.sinkType = GraphNodeType::SURFACE_OUTPUT;
    surface.inputStructName = "SurfaceInputs";
    surface.passes = {std::move(gbuffer), std::move(diffuse)};

    registerDomain(std::move(surface));
}

} // namespace Rapture
