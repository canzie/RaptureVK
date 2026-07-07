#ifndef RAPTURE__GRAPH_INSTANCE_DATA_H
#define RAPTURE__GRAPH_INSTANCE_DATA_H

#include <cstdint>
#include <vector>

namespace Rapture {

// One graph material instance's values, tightly packed as uints into the shared graph arena.
// The compiler assigns each value a uint offset within this slice: a texture stores its bindless
// index, a scalar or vector stores its component bit patterns.
using GraphInstanceData = std::vector<uint32_t>;

} // namespace Rapture

#endif // RAPTURE__GRAPH_INSTANCE_DATA_H
