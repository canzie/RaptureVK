#ifndef RAPTURE__CHUNK_GRID_H
#define RAPTURE__CHUNK_GRID_H

#include "TerrainTypes.h"

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace Rapture {

/**
 * @brief Sparse 2D storage for terrain chunks.
 *
 * Provides efficient lookup by grid coordinate and spatial queries.
 * Only loaded chunks exist in the map.
 */
class ChunkGrid {
  public:
    ChunkGrid() = default;
    ~ChunkGrid() = default;

    // Basic operations
    void setChunk(glm::ivec2 coord, TerrainChunk &&chunk);
    TerrainChunk *getChunk(glm::ivec2 coord);
    const TerrainChunk *getChunk(glm::ivec2 coord) const;
    void removeChunk(glm::ivec2 coord);
    bool hasChunk(glm::ivec2 coord) const;
    void clear();

    // Iteration
    template <typename Func> void forEach(Func &&fn)
    {
        for (auto &[coord, chunk] : m_chunks) {
            fn(coord, chunk);
        }
    }

    template <typename Func> void forEach(Func &&fn) const
    {
        for (const auto &[coord, chunk] : m_chunks) {
            fn(coord, chunk);
        }
    }

    // Queries
    size_t size() const { return m_chunks.size(); }
    bool empty() const { return m_chunks.empty(); }

    // Spatial queries
    std::vector<TerrainChunk *> getChunksInRadius(glm::ivec2 center, int32_t radius);
    std::vector<TerrainChunk *> getChunksInRect(glm::ivec2 min, glm::ivec2 max);

    // Get all chunks in a specific state
    std::vector<TerrainChunk *> getChunksByState(TerrainChunk::State state);

    // Direct access to underlying map (for advanced iteration)
    auto begin() { return m_chunks.begin(); }
    auto end() { return m_chunks.end(); }
    auto begin() const { return m_chunks.begin(); }
    auto end() const { return m_chunks.end(); }

  private:
    std::unordered_map<glm::ivec2, TerrainChunk> m_chunks;
};

} // namespace Rapture

#endif // RAPTURE__CHUNK_GRID_H
