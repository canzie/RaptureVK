#ifndef RAPTURE__TERRAIN_TYPES_H
#define RAPTURE__TERRAIN_TYPES_H

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace Rapture {

constexpr uint32_t TERRAIN_NOISE_LUT_SIZE = 16;

enum TerrainNoiseCategory : uint8_t {
    CONTINENTALNESS = 0,
    EROSION = 1,
    PEAKS_VALLEYS = 2,
    TERRAIN_NC_COUNT
};

struct TerrainSpline {
    std::vector<glm::vec2> points;
};

struct MultiNoiseConfig {
    TerrainSpline splines[TERRAIN_NC_COUNT];
};

// LOD System Constants
constexpr uint32_t TERRAIN_LOD_COUNT = 4;
constexpr uint32_t TERRAIN_INVALID_LOD = UINT32_MAX;

// LOD resolutions (vertices per edge): power-of-2 + 1 for seamless subdivision
constexpr uint32_t TERRAIN_LOD_RESOLUTIONS[TERRAIN_LOD_COUNT] = {
    129, // LOD0 (highest detail)
    65,  // LOD1
    33,  // LOD2
    17   // LOD3 (lowest detail)
};

// LOD distance thresholds (world units from camera)
constexpr float TERRAIN_LOD_DISTANCES[TERRAIN_LOD_COUNT] = {
    128.0f,  // LOD0: closest
    256.0f,  // LOD1
    512.0f,  // LOD2
    1024.0f  // LOD3: farthest
};

// Helper functions for LOD system
inline constexpr uint32_t getTerrainLODResolution(uint32_t lod)
{
    return (lod < TERRAIN_LOD_COUNT) ? TERRAIN_LOD_RESOLUTIONS[lod] : TERRAIN_LOD_RESOLUTIONS[TERRAIN_LOD_COUNT - 1];
}

inline constexpr uint32_t getTerrainLODVertexCount(uint32_t lod)
{
    uint32_t res = getTerrainLODResolution(lod);
    return res * res;
}

inline constexpr uint32_t getTerrainLODIndexCount(uint32_t lod)
{
    uint32_t quads = getTerrainLODResolution(lod) - 1;
    return quads * quads * 6;
}

inline uint32_t calculateTerrainLOD(float distance)
{
    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        if (distance < TERRAIN_LOD_DISTANCES[lod]) {
            return lod;
        }
    }
    return TERRAIN_LOD_COUNT - 1;
}


/**
 * @brief GPU-side chunk data for vertex shader and compute shader.
 * This is stored in an SSBO and indexed by chunk ID.
 * Must match the shader struct layout.
 */
struct alignas(16) TerrainChunkGPUData {
    glm::vec2 worldOffset;  // World position of chunk corner
    float chunkSize;        // World size of chunk edge
    uint32_t lod;           // Current LOD level

    glm::vec4 bounds;       // minX, minZ, maxX, maxZ (for culling)
    float minHeight;        // Min Y for AABB culling
    float maxHeight;        // Max Y for AABB culling
    uint32_t neighborLODs;  // Packed neighbor LOD info for seam stitching
    uint32_t flags;         // Visibility flags, etc.
};

/**
 * @brief CPU-side chunk metadata.
 * Lightweight - no GPU buffers, just tracking info.
 */
struct TerrainChunk {
    glm::ivec2 coord{0, 0};     // Grid coordinate
    uint32_t lod = 0;           // Current LOD level
    uint32_t chunkIndex = 0;    // Index in chunk data SSBO

    // Bounds for CPU-side culling (optional, can also do GPU-only)
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    // State
    enum class State : uint8_t {
        Unloaded,
        Active,         // In the GPU chunk list, ready to render
        PendingUnload
    };
    State state = State::Unloaded;

    bool isActive() const { return state == State::Active; }
};

/**
 * @brief Configuration for terrain system.
 */
struct TerrainConfig {
    float chunkWorldSize = 64.0f;     // World units per chunk edge
    float heightScale = 100.0f;       // Maximum terrain height
    float terrainWorldSize = 1024.0f; // Total terrain size for heightmap mapping
    uint32_t maxLoadedChunks = 256;   // Maximum chunks that can be loaded

    // Derived: spacing at each LOD
    float getVertexSpacing(uint32_t lod) const
    {
        return chunkWorldSize / static_cast<float>(getTerrainLODResolution(lod) - 1);
    }
};

} // namespace Rapture

// Hash for glm::ivec2 (for unordered_map usage)
namespace std {
template <> struct hash<glm::ivec2> {
    size_t operator()(const glm::ivec2 &v) const noexcept
    {
        size_t h1 = hash<int>{}(v.x);
        size_t h2 = hash<int>{}(v.y);
        return h1 ^ (h2 << 1);
    }
};
} // namespace std

#endif // RAPTURE__TERRAIN_TYPES_H
