#include "ChunkGrid.h"

namespace Rapture {

void ChunkGrid::setChunk(glm::ivec2 coord, TerrainChunk &&chunk)
{
    m_chunks[coord] = std::move(chunk);
}

TerrainChunk *ChunkGrid::getChunk(glm::ivec2 coord)
{
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

const TerrainChunk *ChunkGrid::getChunk(glm::ivec2 coord) const
{
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

void ChunkGrid::removeChunk(glm::ivec2 coord)
{
    m_chunks.erase(coord);
}

bool ChunkGrid::hasChunk(glm::ivec2 coord) const
{
    return m_chunks.find(coord) != m_chunks.end();
}

void ChunkGrid::clear()
{
    m_chunks.clear();
}

std::vector<TerrainChunk *> ChunkGrid::getChunksInRadius(glm::ivec2 center, int32_t radius)
{
    std::vector<TerrainChunk *> result;

    for (int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            if (auto *chunk = getChunk({x, y})) {
                result.push_back(chunk);
            }
        }
    }

    return result;
}

std::vector<TerrainChunk *> ChunkGrid::getChunksInRect(glm::ivec2 min, glm::ivec2 max)
{
    std::vector<TerrainChunk *> result;

    for (int32_t y = min.y; y <= max.y; ++y) {
        for (int32_t x = min.x; x <= max.x; ++x) {
            if (auto *chunk = getChunk({x, y})) {
                result.push_back(chunk);
            }
        }
    }

    return result;
}

std::vector<TerrainChunk *> ChunkGrid::getChunksByState(TerrainChunk::State state)
{
    std::vector<TerrainChunk *> result;

    for (auto &[coord, chunk] : m_chunks) {
        if (chunk.state == state) {
            result.push_back(&chunk);
        }
    }

    return result;
}

} // namespace Rapture
