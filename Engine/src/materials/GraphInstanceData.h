#ifndef RAPTURE__GRAPH_INSTANCE_DATA_H
#define RAPTURE__GRAPH_INSTANCE_DATA_H

#include <cstdint>
#include <glm/glm.hpp>

namespace Rapture {

// Generic per-instance pool for a graph material. Slots have no fixed meaning -
// the graph compiler assigns them (noise texture -> textures[k], a tint -> constants[j]).
// Fixed-array Option A; migrates to a variable-length SSBO slice when outgrown.

constexpr uint32_t GRAPH_MAX_TEXTURES = 16;
constexpr uint32_t GRAPH_MAX_CONSTANTS = 16;

struct alignas(16) GraphInstanceData {
    uint32_t textures[GRAPH_MAX_TEXTURES];   // bindless texture indices, compiler-assigned
    glm::vec4 constants[GRAPH_MAX_CONSTANTS]; // constants + exposed params, compiler-assigned

    static GraphInstanceData createDefault()
    {
        GraphInstanceData data{};
        return data;
    }
};

static_assert(sizeof(GraphInstanceData) == 320, "GraphInstanceData must match its std430 layout");

} // namespace Rapture

#endif // RAPTURE__GRAPH_INSTANCE_DATA_H
