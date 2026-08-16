#ifndef RAPTURE__MATERIAL_H
#define RAPTURE__MATERIAL_H

#include "MaterialData.h"
#include "MaterialParameters.h"
#include "assets/asset_manager/AssetCommon.h"
#include "assets/asset_manager/AssetHandle.h"
#include "gpu/buffers/VirtualStorageBuffer.h"
#include "core/events/Events.h"
#include "graph/MaterialGraph.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

class FreeListStorageBuffer;
class SurfaceGraphManager;

// Maximum number of live material instances backed by the shared SSBO arena
constexpr uint32_t MAX_MATERIALS = 4096;

class BaseMaterial {
  public:
    BaseMaterial(std::string name, uint32_t graphId, std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph);
    ~BaseMaterial() = default;

    const std::string &getName() const { return m_name; }
    uint32_t getGraphId() const { return m_graphId; }

    /**
     * @brief Serializes this base material's graph and parameter table into a self-contained blob
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Rebuilds a base material from a blob, recompiling its graph to obtain a graph id
     * @param blob The serialized bytes
     * @return The base material, or nullptr if the blob is invalid
     */
    static std::unique_ptr<BaseMaterial> deserialize(std::span<const uint8_t> blob);

    /**
     * @brief The authored graph this base was compiled from, retained so the editor can redraw it
     * @return The source graph
     */
    const MaterialGraph &getGraph() const { return m_graph; }

    /**
     * @brief Resolve the slice offset a parameter writes to
     * @param id The parameter to resolve
     * @param out Set to the uint offset within the instance slice when found
     * @return True if this base exposes the parameter
     */
    bool tryGetOffset(const ParameterId &id, uint32_t &out) const;

  private:
    std::string m_name;
    uint32_t m_graphId;
    std::unordered_map<ParameterId, uint32_t> m_table;
    MaterialGraph m_graph;

    friend class MaterialInstance;
    friend class MaterialManager;
};

class MaterialManager {
  public:
    static void init();

    /**
     * @brief Releases graph resources holding asset references, so they free while the AssetManager is still alive
     */
    static void releaseGraphResources();

    static void shutdown();

    static AssetPtr<BaseMaterial> getMaterial(const std::string &name);
    static AssetPtr<BaseMaterial> createMaterial(const std::string &name, uint32_t graphId,
                                                 std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph,
                                                 std::filesystem::path outputFolder);
    static AssetPtr<BaseMaterial> createBuiltinMaterial(const std::string &name, uint32_t graphId,
                                                        std::unordered_map<ParameterId, uint32_t> table, MaterialGraph graph,
                                                        AssetHandle reservedHandle);
    static uint32_t getDefaultTextureIndex();
    static void printMaterialNames();

    /**
     * @brief Reserve a slot in the shared MaterialData SSBO
     * @return Slot index, or UINT32_MAX if the arena is full
     */
    static uint32_t allocateSlot();

    /**
     * @brief Release a previously allocated slot back to the arena
     * @param slot Slot index to release
     */
    static void freeSlot(uint32_t slot);

    /**
     * @brief Write a material's data into its slot in the shared SSBO
     * @param slot Slot index to write
     * @param data Material data to upload
     */
    static void writeSlot(uint32_t slot, const MaterialData &data);

    /**
     * @brief Reserve a range in the graph data arena
     * @param sizeBytes Byte size of the instance slice
     * @return The range, invalid if the arena is full
     */
    static VirtualStorageBuffer::Allocation allocateGraphData(uint32_t sizeBytes);

    /**
     * @brief Release a previously allocated graph data range, leaving it invalid
     * @param allocation The range returned by allocateGraphData
     */
    static void freeGraphData(VirtualStorageBuffer::Allocation &allocation);

    /**
     * @brief Write a graph instance's packed slice into the arena
     * @param allocation The range returned by allocateGraphData
     * @param data The packed uints to upload
     * @param uintOffset Uint offset within the range to start writing at
     */
    static void writeGraphData(const VirtualStorageBuffer::Allocation &allocation, std::span<const uint32_t> data,
                               uint32_t uintOffset = 0);

    /**
     * @brief Access the owned surface graph manager
     * @return The manager
     */
    static SurfaceGraphManager &getSurfaceGraphManager();

  private:
    static void createDefaultMaterials();

    static bool s_initialized;
    static uint32_t s_defaultTextureIndex;
    static std::unordered_map<std::string, AssetHandle> s_materialHandles;
    static std::unique_ptr<FreeListStorageBuffer> s_materialBuffer;
    static std::unique_ptr<VirtualStorageBuffer> s_graphBuffer;
    static std::unique_ptr<SurfaceGraphManager> s_surfaceGraphManager;
    static EventListenerId s_serializeListener;
    static EventListenerId s_registerListener;
    static EventListenerId s_registerCompleteListener;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_H
