#ifndef RAPTURE__GRAPH_DOMAIN_H
#define RAPTURE__GRAPH_DOMAIN_H

#include <string_view>
#include <vector>

#include "MaterialGraphTypes.h"

namespace Rapture {

/**
 * @brief One field of a pass's generated output struct
 *
 * A bound field reads the sink input pin of the same name, coerced to type; a constant field has
 * no pin and always emits fallback. glslTypeOverride sets the struct field type when it is not a
 * PinType (e.g. the uint shading model id). All views point at static literals from the domain
 * descriptors, never owned strings.
 */
struct GraphOutputField {
    std::string_view name = {};
    PinType type = PinType::FLOAT;
    std::string_view fallback = {};         // value when the field's sink pin is unconnected in a known graph
    std::string_view errorFallback = {};    // value in the unknown-graph dispatcher fallback, empty uses fallback
    bool constant = false;                  // no sink pin, always the fallback
    std::string_view glslTypeOverride = {}; // struct field type when it is not a PinType
};

/**
 * @brief One generated GLSL pass of a domain: an output struct, a dispatcher, and its target file
 *
 * Each graph compiles to one function per pass, all reading the same instance slice. The struct
 * definition, the per-field assignments, and the dispatcher fallback all derive from fields.
 */
struct GraphPass {
    std::string_view id;             // baked into the function name, e.g. "gbuffer"
    std::string_view structName;     // output struct, e.g. "SurfaceData"
    std::string_view funcPrefix;     // per-graph function prefix, e.g. "evalSurface_"
    std::string_view dispatcherName; // e.g. "evalSurfaceGraph"
    std::string_view fileName;       // generated file, e.g. "SurfaceGraphs.glsl"
    std::string_view guard;          // include guard macro
    std::vector<GraphOutputField> fields;
};

/**
 * @brief A material family: a sink node type, the input struct its functions read, and its passes
 */
struct GraphDomain {
    std::string_view name;
    GraphNodeType sinkType = GraphNodeType::SURFACE_OUTPUT;
    std::string_view inputStructName; // generated function parameter type, e.g. "SurfaceInputs"
    std::vector<GraphPass> passes;    // passes[0] is the full pass
};

/**
 * @brief Catalogue of graph domains keyed by sink node type
 *
 * A graph's domain is resolved from its output node type, so the compiler and the manager iterate
 * domains and passes generically instead of special casing each surface variant.
 */
class GraphDomainRegistry {
  public:
    /**
     * @brief Look up the domain owning a sink node type
     * @param sinkType The graph's output node type
     * @return The domain, or nullptr if the type is not a registered sink
     */
    static const GraphDomain *forSink(GraphNodeType sinkType);

    /**
     * @brief All registered domains, for enumerating passes and generated files
     * @return The domain list
     */
    static const std::vector<GraphDomain> &all();

    /**
     * @brief Add or replace a domain (keyed by its sink type)
     * @param domain The domain to register
     */
    static void registerDomain(GraphDomain domain);

    /**
     * @brief Register the built-in node set and the Surface domain (idempotent)
     */
    static void registerBuiltins();
};

} // namespace Rapture

#endif // RAPTURE__GRAPH_DOMAIN_H
