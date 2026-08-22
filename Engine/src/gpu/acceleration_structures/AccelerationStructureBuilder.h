#ifndef RAPTURE__ACCELERATION_STRUCTURE_BUILDER_H
#define RAPTURE__ACCELERATION_STRUCTURE_BUILDER_H

#include "assets/asset_manager/AssetCommon.h"
#include "assets/asset_manager/AssetHandle.h"
#include "core/events/EventSignal.h"

#include <cstdint>
#include <mutex>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Rapture {

class BLAS;
class CommandBuffer;
class Mesh;
struct JobContext;

/**
 * @brief Builds and compacts the bottom level structures of meshes that ask for one
 *
 * A mesh is handed over with enqueue, and its structure reports itself ready once it has been
 * built and compacted.
 */
class AccelerationStructureBuilder {
  public:
    AccelerationStructureBuilder();
    ~AccelerationStructureBuilder();

    AccelerationStructureBuilder(const AccelerationStructureBuilder &) = delete;
    AccelerationStructureBuilder &operator=(const AccelerationStructureBuilder &) = delete;

    /**
     * @brief Takes a mesh whose structure has been created but not yet built
     * @param mesh The mesh to build for, kept alive until its structure is ready
     * @param assetHandle The mesh's asset, referenced for as long as the build runs
     */
    void enqueue(Mesh &mesh, AssetHandle assetHandle);

    /**
     * @brief Fires from the building job once a batch of structures has become ready
     */
    EventSignal<void()> onStructuresReady;

  private:
    struct PendingMesh {
        Mesh *mesh = nullptr;
        AssetRef ref;
    };

    /**
     * @brief Takes meshes off the pending list until their scratch reaches the budget
     * @param scratchBytes Filled with the scratch the returned batch needs, aligned per entry
     * @return The meshes to build together, empty when nothing is pending
     */
    std::vector<PendingMesh> takeBatch(VkDeviceSize &scratchBytes);

    /**
     * @brief Builds and compacts one batch, yielding its fiber over both GPU round trips
     * @param jobContext The context of the job draining the pending list
     */
    void drainBatch(JobContext &jobContext);

    /**
     * @brief Starts a drain job unless one is already running
     */
    void kickDrain();

    /**
     * @brief Frees the scratch buffer and query pool, which only have to exist while a batch is building
     */
    void releaseBuildResources();

    /**
     * @brief Takes a command buffer from the pool of the thread the job is running on right now
     * @return The command buffer to record into
     */
    CommandBuffer *takeCommandBuffer();

    /**
     * @brief Grows the shared scratch buffer to hold a batch
     * @param sizeBytes Scratch the batch needs
     * @return The aligned device address of the scratch buffer, or 0 on failure
     */
    VkDeviceAddress reserveScratch(VkDeviceSize sizeBytes);

  private:
    std::mutex m_pendingMutex;
    std::vector<PendingMesh> m_pending;
    bool m_isDraining = false;

    VkBuffer m_scratchBuffer = VK_NULL_HANDLE;
    VmaAllocation m_scratchAllocation = VK_NULL_HANDLE;
    VkDeviceSize m_scratchCapacityBytes = 0;

    VkQueryPool m_compactedSizeQueryPool = VK_NULL_HANDLE;
    uint32_t m_queryPoolCapacity = 0;

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__ACCELERATION_STRUCTURE_BUILDER_H
