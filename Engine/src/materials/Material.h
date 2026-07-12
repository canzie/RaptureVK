#ifndef RAPTURE__MATERIAL_H
#define RAPTURE__MATERIAL_H

#include "MaterialData.h"
#include "MaterialParameters.h"
#include "events/ProjectEvents.h"
#include "graph/MaterialGraph.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Rapture {

class FreeListStorageBuffer;
class VirtualStorageBuffer;
class SurfaceGraphManager;

// Maximum number of live material instances backed by the shared SSBO arena
constexpr uint32_t MAX_MATERIALS = 4096;

class BaseMaterial : public std::enable_shared_from_this<BaseMaterial> {
  public:
    BaseMaterial(std::string name, uint32_t graphId, std::unordered_map<ParameterID, uint32_t> table, MaterialGraph graph);
    ~BaseMaterial() = default;

    const std::string &getName() const { return m_name; }
    uint32_t getGraphId() const { return m_graphId; }

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
    bool tryGetOffset(ParameterID id, uint32_t &out) const;

  private:
    std::string m_name;
    uint32_t m_graphId;
    std::unordered_map<ParameterID, uint32_t> m_table;
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

    static std::shared_ptr<BaseMaterial> getMaterial(const std::string &name);
    static std::shared_ptr<BaseMaterial> createMaterial(const std::string &name, uint32_t graphId,
                                                        std::unordered_map<ParameterID, uint32_t> table, MaterialGraph graph);
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
     * @return The uint offset of the range, or UINT32_MAX if the arena is full
     */
    static uint32_t allocateGraphData(uint32_t sizeBytes);

    /**
     * @brief Release a previously allocated graph data range
     * @param uintOffset The uint offset returned by allocateGraphData
     */
    static void freeGraphData(uint32_t uintOffset);

    /**
     * @brief Write a graph instance's packed slice into the arena
     * @param uintOffset The uint offset returned by allocateGraphData
     * @param data Pointer to the packed uints to upload
     * @param sizeBytes Byte size to write
     */
    static void writeGraphData(uint32_t uintOffset, const void *data, uint32_t sizeBytes);

    /**
     * @brief Access the owned surface graph manager
     * @return The manager
     */
    static SurfaceGraphManager &getSurfaceGraphManager();

  private:
    static void createDefaultMaterials();

    static bool s_initialized;
    static uint32_t s_defaultTextureIndex;
    static std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> s_materials;
    static std::unique_ptr<FreeListStorageBuffer> s_materialBuffer;
    static std::unique_ptr<VirtualStorageBuffer> s_graphBuffer;
    static std::unique_ptr<SurfaceGraphManager> s_surfaceGraphManager;
    static EventListenerId s_serializeListener;
    static EventListenerId s_registerListener;
    static EventListenerId s_registerCompleteListener;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_H
