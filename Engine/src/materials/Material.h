#ifndef RAPTURE__MATERIAL_H
#define RAPTURE__MATERIAL_H

#include "GraphInstanceData.h"
#include "MaterialData.h"
#include "MaterialParameters.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Rapture {

class FreeListStorageBuffer;
class SurfaceGraphManager;

// Maximum number of live material instances backed by the shared SSBO arena
constexpr uint32_t MAX_MATERIALS = 4096;

// Maximum number of live graph material instances backed by the graph data arena
constexpr uint32_t MAX_GRAPH_MATERIALS = 1024;

class BaseMaterial : public std::enable_shared_from_this<BaseMaterial> {
  public:
    BaseMaterial(const std::string &name, std::initializer_list<ParameterID> editableParams, const MaterialData &defaults);
    ~BaseMaterial() = default;

    const std::string &getName() const { return m_name; }
    const MaterialData &getDefaults() const { return m_defaults; }
    bool canEdit(ParameterID id) const { return m_editableParams.find(id) != m_editableParams.end(); }
    const std::unordered_set<ParameterID> &getEditableParams() const { return m_editableParams; }

  private:
    std::string m_name;
    std::unordered_set<ParameterID> m_editableParams;
    MaterialData m_defaults;

    friend class MaterialInstance;
    friend class MaterialManager;
};

class MaterialManager {
  public:
    static void init();
    static void shutdown();

    static std::shared_ptr<BaseMaterial> getMaterial(const std::string &name);
    static std::shared_ptr<BaseMaterial> createMaterial(const std::string &name, std::initializer_list<ParameterID> editableParams,
                                                        const MaterialData &defaults);
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
     * @brief Reserve a slot in the graph data arena
     * @return Slot index, or UINT32_MAX if the arena is full
     */
    static uint32_t allocateGraphSlot();

    /**
     * @brief Release a previously allocated graph data slot
     * @param slot Slot index to release
     */
    static void freeGraphSlot(uint32_t slot);

    /**
     * @brief Write a graph instance's data into its slot in the graph data arena
     * @param slot Slot index to write
     * @param data Graph instance data to upload
     */
    static void writeGraphSlot(uint32_t slot, const GraphInstanceData &data);

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
    static std::unique_ptr<FreeListStorageBuffer> s_graphBuffer;
    static std::unique_ptr<SurfaceGraphManager> s_surfaceGraphManager;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_H
