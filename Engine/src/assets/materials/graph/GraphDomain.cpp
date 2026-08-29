#include "GraphDomain.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "NodeRegistry.h"
#include "core/utils/rp_assert.h"

namespace Rapture {

static std::vector<GraphDomain> s_domains;
static bool s_builtinsRegistered = false;

const char *Graph_domainName(GraphDomainId id)
{
    switch (id) {
#define X(name) \
    case name:  \
        return #name;
        RP_GRAPH_DOMAINS(X)
#undef X
    default:
        return "GD_SURFACE";
    }
}

GraphDomainId Graph_domainFromName(std::string_view name)
{
    static const std::unordered_map<std::string_view, GraphDomainId> names = {
#define X(name) {#name, name},
        RP_GRAPH_DOMAINS(X)
#undef X
    };
    auto it = names.find(name);
    return it == names.end() ? GD_SURFACE : it->second;
}

const GraphInputDef *GraphDomain::findInput(std::string_view name) const
{
    for (const auto &input : inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

bool Graph_nodeFitsDomain(const NodeDefinition &def, const GraphDomain &domain, std::string_view *missing)
{
    for (const auto &required : def.requiredInputs) {
        if (domain.findInput(required) == nullptr) {
            if (missing != nullptr) {
                *missing = required;
            }
            return false;
        }
    }
    return true;
}

std::string Graph_substituteDomainInputs(std::string &expr, const GraphDomain &domain)
{
    size_t pos = 0;
    while ((pos = expr.find("{$", pos)) != std::string::npos) {
        size_t end = expr.find('}', pos);
        if (end == std::string::npos) {
            return expr.substr(pos);
        }

        std::string_view name(expr.data() + pos + 2, end - pos - 2);
        const GraphInputDef *input = domain.findInput(name);
        if (input == nullptr) {
            return std::string(name);
        }

        expr.replace(pos, end - pos + 1, input->glslExpr);
        pos += input->glslExpr.size();
    }
    return {};
}

/**
 * @brief Assert a domain is internally consistent before anything compiles against it
 *
 * Three ways a domain can be wrong, all silent at runtime: its bound fields drifting from the sink
 * pins they name, duplicate input names shadowing each other, and a field fallback referencing an
 * input the domain never declared.
 */
static void s_validateDomain(const GraphDomain &domain)
{
    const char *domainName = Graph_domainName(domain.id);

    std::unordered_set<std::string_view> inputNames;
    for (const auto &input : domain.inputs) {
        [[maybe_unused]] bool unique = inputNames.insert(input.name).second;
        RP_ASSERT(unique, "graph domain '{}' declares the input '{}' more than once", domainName, input.name);
    }

    const NodeDefinition *sink = NodeRegistry::get(domain.sinkType);
    RP_ASSERT(sink != nullptr, "graph domain '{}' has no registered definition for its sink node type '{}'", domainName,
              Graph_nodeTypeName(domain.sinkType));
    if (sink == nullptr) {
        return;
    }

    for (const auto &pass : domain.passes) {
        for (const auto &field : pass.fields) {
            // A fallback is emitted verbatim into both the pass function and the dispatcher, so it
            // reads the domain's inputs through the same {$name} indirection a node template does
            for (std::string_view text : {field.fallback, field.errorFallback}) {
                std::string probe(text);
                std::string missing = Graph_substituteDomainInputs(probe, domain);
                RP_ASSERT(missing.empty(),
                          "graph domain '{}' pass '{}' field '{}' falls back on the input '{}', which the domain does not "
                          "provide",
                          domainName, pass.id, field.name, missing);
            }

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
            RP_ASSERT(pin != nullptr, "graph domain '{}' pass '{}' binds field '{}' with no matching sink pin", domainName, pass.id,
                      field.name);
            if (pin == nullptr) {
                continue;
            }
            RP_ASSERT(pin->type == field.type, "graph domain '{}' pass '{}' field '{}' is typed '{}' but its sink pin is '{}'",
                      domainName, pass.id, field.name, graph_pinTypeGlsl(field.type), graph_pinTypeGlsl(pin->type));
        }
    }
}

const GraphDomain *GraphDomainRegistry::forId(GraphDomainId id)
{
    for (const auto &domain : s_domains) {
        if (domain.id == id) {
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
        if (existing.id == domain.id) {
            existing = std::move(domain);
            return;
        }
    }
    s_domains.push_back(std::move(domain));
}

/**
 * @brief The full channel set a SURFACE_OUTPUT sink drives
 *
 * Shared by every domain sinking into SURFACE_OUTPUT. The fallbacks read domain inputs through
 * {$name}, so the same list serves each domain's own input struct.
 */
static std::vector<GraphOutputField> s_surfaceOutputFields()
{
    return {
        {.name = "albedo", .type = PinType::VEC3, .fallback = "vec3(1.0)", .errorFallback = "vec3(1.0, 0.0, 1.0)"},
        {.name = "normal", .type = PinType::VEC3, .fallback = "normalize({$worldNormal})"},
        {.name = "roughness", .type = PinType::FLOAT, .fallback = "0.5"},
        {.name = "metallic", .type = PinType::FLOAT, .fallback = "0.0"},
        {.name = "ao", .type = PinType::FLOAT, .fallback = "1.0"},
        // Dielectric F0, already folded from OpenPBR's specular_weight and specular_ior. 0.04 is ior 1.5
        {.name = "specular", .type = PinType::FLOAT, .fallback = "0.04"},
        {.name = "emission", .type = PinType::VEC4, .fallback = "vec4(1.0)", .errorFallback = "vec4(0.0)"},
        {.name = "emissiveStrength", .type = PinType::FLOAT, .fallback = "0.0"},
        {.name = "shadingModelId", .fallback = "SM_OPENPBR_STANDARD", .constant = true, .glslTypeOverride = "uint"},
    };
}

/**
 * @brief The Surface domain: mesh geometry with a uv and a tangent frame, rastered and ray traced
 */
static void s_registerSurfaceDomain()
{
    // The full G-buffer pass: every surface channel, plus the constant shading model id. albedo and
    // emission carry a debug errorFallback used only by the unknown-graph dispatcher case.
    GraphPass gbuffer;
    gbuffer.id = "gbuffer";
    gbuffer.structName = "SurfaceData";
    gbuffer.funcPrefix = "evalSurface_";
    gbuffer.dispatcherName = "evalSurfaceGraph";
    gbuffer.fileName = "SurfaceGraphs.glsl";
    gbuffer.guard = "SURFACE_GRAPHS_GLSL";
    gbuffer.fields = s_surfaceOutputFields();

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
        {.name = "normal", .type = PinType::VEC3, .fallback = "normalize({$worldNormal})"},
        {.name = "emission", .type = PinType::VEC4, .fallback = "vec4(1.0)", .errorFallback = "vec4(0.0)"},
        {.name = "emissiveStrength", .type = PinType::FLOAT, .fallback = "0.0"},
    };

    GraphDomain surface;
    surface.id = GD_SURFACE;
    surface.sinkType = GraphNodeType::SURFACE_OUTPUT;
    surface.inputStructName = "SurfaceInputs";
    surface.inputs = {
        {.name = "uv", .type = PinType::VEC2, .glslExpr = "si.uv"},
        {.name = "worldPos", .type = PinType::VEC3, .glslExpr = "si.worldPos"},
        {.name = "worldNormal", .type = PinType::VEC3, .glslExpr = "si.worldNormal"},
        {.name = "tangent", .type = PinType::VEC3, .glslExpr = "si.tangent"},
        {.name = "bitangent", .type = PinType::VEC3, .glslExpr = "si.bitangent"},
    };
    surface.passes = {std::move(gbuffer), std::move(diffuse)};

    GraphDomainRegistry::registerDomain(std::move(surface));
}

/**
 * @brief The Terrain domain: heightmap chunks with no vertex attributes, raster only
 *
 * A sampled noise field costs nothing unless a node reads it, since an input is an expression
 * substituted into the node that names it, not a field computed ahead of the call.
 */
static void s_registerTerrainDomain()
{
    GraphPass gbuffer;
    gbuffer.id = "gbuffer";
    gbuffer.structName = "TerrainSurfaceData";
    gbuffer.funcPrefix = "evalTerrainSurface_";
    gbuffer.dispatcherName = "evalTerrainSurfaceGraph";
    gbuffer.fileName = "TerrainGraphs.glsl";
    gbuffer.guard = "TERRAIN_GRAPHS_GLSL";
    gbuffer.fields = s_surfaceOutputFields();

    GraphDomain terrain;
    terrain.id = GD_TERRAIN;
    terrain.sinkType = GraphNodeType::SURFACE_OUTPUT;
    terrain.inputStructName = "TerrainInputs";
    terrain.inputs = {
        {.name = "uv", .type = PinType::VEC2, .glslExpr = "si.uv"},
        {.name = "worldPos", .type = PinType::VEC3, .glslExpr = "si.worldPos"},
        {.name = "worldNormal", .type = PinType::VEC3, .glslExpr = "si.worldNormal"},
        {.name = "normalizedHeight", .type = PinType::FLOAT, .glslExpr = "si.normalizedHeight"},
        {.name = "curvature", .type = PinType::FLOAT, .glslExpr = "si.curvature"},
        {.name = "chunkLod", .type = PinType::INT, .glslExpr = "int(si.lod)"},
        {.name = "continentalness",
         .type = PinType::FLOAT,
         .glslExpr = "texture(u_textures[nonuniformEXT(si.continentalnessTex)], si.uv).r"},
        {.name = "erosion", .type = PinType::FLOAT, .glslExpr = "texture(u_textures[nonuniformEXT(si.erosionTex)], si.uv).r"},
        {.name = "peaksValleys",
         .type = PinType::FLOAT,
         .glslExpr = "texture(u_textures[nonuniformEXT(si.peaksValleysTex)], si.uv).r"},
    };
    terrain.passes = {std::move(gbuffer)};

    GraphDomainRegistry::registerDomain(std::move(terrain));
}

void GraphDomainRegistry::registerBuiltins()
{
    NodeRegistry::registerBuiltins();
    if (s_builtinsRegistered) {
        return;
    }
    s_builtinsRegistered = true;

    s_registerSurfaceDomain();
    s_registerTerrainDomain();
}

} // namespace Rapture
