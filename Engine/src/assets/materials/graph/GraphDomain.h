#ifndef RAPTURE__GRAPH_DOMAIN_H
#define RAPTURE__GRAPH_DOMAIN_H

#include <string>
#include <string_view>
#include <vector>

#include "MaterialGraphTypes.h"

namespace Rapture {

/**
 * @brief The material families a graph can be authored against
 *
 * A domain is an input set plus an output pass set. Two domains may share a sink node type when
 * they produce the same channels from different inputs.
 */
enum GraphDomainId {
    GD_SURFACE,
    GD_TERRAIN,
    GD_COUNT
};

#define RP_GRAPH_DOMAINS(X) X(GD_SURFACE) X(GD_TERRAIN)

/**
 * @brief Stable serialization name for a domain
 * @param id The domain id
 * @return The enum spelling of the id, "GD_SURFACE" for an unknown value
 */
const char *Graph_domainName(GraphDomainId id);

/**
 * @brief Resolve a domain from its serialization name
 * @param name The enum spelling to look up
 * @return The matching domain, or GD_SURFACE when the name is unknown
 */
GraphDomainId Graph_domainFromName(std::string_view name);

/**
 * @brief One named value a domain hands to node templates, referenced as {$name}
 */
struct GraphInputDef {
    std::string_view name = {};
    PinType type = PinType::FLOAT;
    std::string_view glslExpr = {}; // how this pass's function reads it, e.g. "si.worldPos"
};

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
 * @brief A material family: a sink node type, the inputs it provides, and its passes
 */
struct GraphDomain {
    GraphDomainId id = GD_SURFACE;
    GraphNodeType sinkType = GraphNodeType::SURFACE_OUTPUT;
    std::string_view inputStructName;  // generated function parameter type, e.g. "SurfaceInputs"
    std::vector<GraphInputDef> inputs; // what {$name} in a node template may resolve to
    std::vector<GraphPass> passes;     // passes[0] is the full pass

    /**
     * @brief Look up an input this domain provides
     * @param name The input name a template referenced
     * @return The input, or nullptr when this domain does not provide it
     */
    const GraphInputDef *findInput(std::string_view name) const;
};

/**
 * @brief Whether a domain provides every input a node's templates reference
 * @param def The node definition to test
 * @param domain The domain to test against
 * @param missing When non-null, set to the first input name the domain lacks
 * @return True if the node can compile in this domain
 */
bool Graph_nodeFitsDomain(const NodeDefinition &def, const GraphDomain &domain, std::string_view *missing = nullptr);

/**
 * @brief Rewrite every {$name} in an expression to the domain's expression for that input
 * @param expr The expression to rewrite in place
 * @param domain The domain providing the inputs
 * @return Empty on success, or the first input name the domain does not provide
 */
std::string Graph_substituteDomainInputs(std::string &expr, const GraphDomain &domain);

/**
 * @brief Catalogue of graph domains keyed by domain id
 *
 * A graph names its domain, so the compiler and the manager iterate domains and passes generically
 * instead of special casing each surface variant.
 */
class GraphDomainRegistry {
  public:
    /**
     * @brief Look up a domain by id
     * @param id The domain to look up
     * @return The domain, or nullptr if it is not registered
     */
    static const GraphDomain *forId(GraphDomainId id);

    /**
     * @brief All registered domains, for enumerating passes and generated files
     * @return The domain list
     */
    static const std::vector<GraphDomain> &all();

    /**
     * @brief Add or replace a domain (keyed by its id)
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
