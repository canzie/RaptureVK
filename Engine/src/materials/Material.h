#ifndef RAPTURE__MATERIAL_H
#define RAPTURE__MATERIAL_H

#include "MaterialData.h"
#include "MaterialParameters.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Rapture {

class FreeListStorageBuffer;
class VirtualStorageBuffer;
class SurfaceGraphManager;

// Maximum number of live material instances backed by the shared SSBO arena
constexpr uint32_t MAX_MATERIALS = 4096;

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
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_H
