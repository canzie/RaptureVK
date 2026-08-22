#ifndef RAPTURE__TLAS_H
#define RAPTURE__TLAS_H

#include "BLAS.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Rapture {

/**
 * @brief The slot of an instance that is not in a top level structure
 */
static constexpr uint32_t INVALID_TLAS_SLOT = UINT32_MAX;

struct TLASInstance {
    BLAS *blas = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    uint32_t mask = 0xFF;
    uint32_t shaderBindingTableRecordOffset = 0;
    VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    uint32_t entityId = 0;
    uint32_t slot = INVALID_TLAS_SLOT;
};

/**
 * @brief Top level acceleration structure over the bottom level structures of a scene
 */
class TLAS {
  public:
    TLAS();
    ~TLAS();

    TLAS(const TLAS &) = delete;
    TLAS &operator=(const TLAS &) = delete;

    /**
     * @brief Puts a bottom level structure into the scene under a slot of its own
     * @param instance The instance to add, its slot member is ignored
     * @return The slot the instance was given, or INVALID_TLAS_SLOT if it was rejected
     */
    uint32_t addInstance(const TLASInstance &instance);

    /**
     * @brief Takes an entity's instance out of the scene and releases its slot
     * @param entityId The entity whose instance should be removed
     */
    void removeInstance(uint32_t entityId);

    /**
     * @brief Removes every instance and releases every slot
     */
    void clear();

    /**
     * @brief Records and submits a full build of the acceleration structure
     */
    void build();

    /**
     * @brief Moves an entity's instance
     * @param entityId The entity whose instance should move
     * @param transform The world transform to place the instance at
     * @return True if the entity has an instance and the transform differs from its current one
     */
    bool setInstanceTransform(uint32_t entityId, const glm::mat4 &transform);

    /**
     * @brief Records and submits a refit covering every transform set since the last call
     */
    void flushInstanceUpdates();

    VkAccelerationStructureKHR getAccelerationStructure() const { return m_accelerationStructure; }
    VkDeviceAddress getDeviceAddress() const { return m_deviceAddress; }
    uint32_t getBindlessIndex() const { return m_bindlessIndex; }
    bool isBuilt() const { return m_isBuilt; }
    bool needsRebuild() const { return m_needsRebuild; }
    uint32_t getInstanceCount() const { return static_cast<uint32_t>(m_instances.size()); }
    const std::vector<TLASInstance> &getInstances() const { return m_instances; }

    /**
     * @brief The length a slot indexed array must have to hold an entry for every live instance
     * @return One past the highest slot ever handed out
     */
    uint32_t getSlotCapacity() const { return m_slotCapacity; }

    /**
     * @brief A counter that changes whenever the set of instances or their slots changes
     * @return The current revision
     */
    uint64_t getRevision() const { return m_revision; }

  private:
    /**
     * @brief Per frame in flight build inputs, so a submitted build is never written over
     */
    struct BuildResources {
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VmaAllocation instanceAllocation = VK_NULL_HANDLE;
        VkDeviceSize instanceCapacityBytes = 0;

        VkBuffer scratchBuffer = VK_NULL_HANDLE;
        VmaAllocation scratchAllocation = VK_NULL_HANDLE;
        VkDeviceSize scratchCapacityBytes = 0;

        VkFence fence = VK_NULL_HANDLE;
        bool isSubmitted = false;
    };

    /**
     * @brief Takes this frame's build inputs, waiting for the build that last used them
     * @return The resources to record the next build against
     */
    BuildResources &acquireBuildResources();

    /**
     * @brief Grows a build resource's instance buffer to hold every instance and fills it
     * @param resources The resources to write into
     * @return The device address of the filled instance buffer, or 0 on failure
     */
    VkDeviceAddress writeInstanceBuffer(BuildResources &resources);

    /**
     * @brief Grows a build resource's scratch buffer to the size the structure needs
     * @param resources The resources to grow
     * @param sizeBytes The scratch size the driver asked for
     * @return The aligned device address of the scratch buffer, or 0 on failure
     */
    VkDeviceAddress reserveScratch(BuildResources &resources, VkDeviceSize sizeBytes);

    /**
     * @brief Creates the acceleration structure and its backing buffer at the current instance count
     * @return True on success
     */
    bool createAccelerationStructure();

    /**
     * @brief Records a build or a refit and submits it against a fence
     * @param mode Whether to build the structure from scratch or refit it in place
     * @param resources The build inputs to read from
     * @param scratchAddress The aligned scratch address the build writes through
     */
    void submitBuild(VkBuildAccelerationStructureModeKHR mode, BuildResources &resources, VkDeviceAddress scratchAddress);

    /**
     * @brief Publishes the structure to the bindless acceleration structure binding
     */
    void registerWithDescriptorManager();

    /**
     * @brief Releases every build resource and its fence
     */
    void destroyBuildResources();

  private:
    std::vector<TLASInstance> m_instances;
    std::unordered_map<uint32_t, uint32_t> m_entityToIndex;
    std::unordered_set<uint32_t> m_dirtyInstances;

    std::vector<uint32_t> m_freeSlots;
    uint32_t m_slotCapacity;
    uint64_t m_revision;

    VkAccelerationStructureKHR m_accelerationStructure;
    VkAccelerationStructureGeometryKHR m_geometry;
    VkAccelerationStructureBuildGeometryInfoKHR m_buildInfo;
    VkAccelerationStructureBuildRangeInfoKHR m_buildRangeInfo;

    VkBuffer m_buffer;
    VmaAllocation m_allocation;

    std::vector<BuildResources> m_buildResources;

    VkDeviceAddress m_deviceAddress;
    VkDeviceSize m_accelerationStructureSize;
    VkDeviceSize m_buildScratchSize;
    VkDeviceSize m_updateScratchSize;

    bool m_isBuilt;
    bool m_needsRebuild;

    VkDevice m_device;
    VmaAllocator m_allocator;

    uint32_t m_bindlessIndex;
};

} // namespace Rapture

#endif // RAPTURE__TLAS_H
