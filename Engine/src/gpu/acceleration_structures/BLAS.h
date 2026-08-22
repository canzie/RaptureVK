#ifndef RAPTURE__BLAS_H
#define RAPTURE__BLAS_H

#include "gpu/buffers/Buffers.h"

#include <atomic>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Rapture {

class Mesh;

/**
 * @brief Bottom level acceleration structure built from a single mesh
 *
 * Owned by the Mesh it is built from, see Mesh::getOrBuildBLAS. The structure is in
 * mesh-local space and therefore scene independent; only the TLAS instance that
 * references it carries a transform.
 */
class BLAS {
  public:
    /**
     * @brief Create the acceleration structure for a mesh
     * @param mesh Mesh to read geometry from; only read during construction, not retained
     */
    explicit BLAS(const Mesh &mesh);

    /**
     * @brief Destroy the acceleration structure and its backing buffers
     */
    ~BLAS();

    // m_buildInfo points at m_geometry, so a copy would describe the geometry of the object it came from
    BLAS(const BLAS &) = delete;
    BLAS &operator=(const BLAS &) = delete;
    BLAS(BLAS &&) = delete;
    BLAS &operator=(BLAS &&) = delete;

    /**
     * @brief Records the build of this structure
     * @param commandBuffer Command buffer to record into
     * @param scratchAddress Device address of scratch space at least getBuildScratchSize bytes long
     */
    void recordBuild(VkCommandBuffer commandBuffer, VkDeviceAddress scratchAddress);

    /**
     * @brief Marks the structure buildable against, once the build recorded for it has completed
     */
    void markBuilt() { m_isBuilt.store(true, std::memory_order_release); }

    /**
     * @brief Marks the structure usable, once it has been built and compacted
     */
    void markReady() { m_isReady.store(true, std::memory_order_release); }

    /**
     * @brief Whether the structure can be traced against
     * @return True once the structure has been built and compacted
     */
    bool isReady() const { return m_isReady.load(std::memory_order_acquire); }

    /**
     * @brief Creates the smaller structure that a compacting copy writes into
     * @param compactedSize Size the compacted structure needs, as reported by the size query
     * @return True on success
     */
    bool createCompacted(VkDeviceSize compactedSize);

    /**
     * @brief Records the copy that fills the compacted structure created by createCompacted
     * @param commandBuffer Command buffer to record into
     */
    void recordCompactCopy(VkCommandBuffer commandBuffer);

    /**
     * @brief Replaces this structure with its compacted copy, once the copy recorded for it has completed
     */
    void adoptCompacted();

    /**
     * @brief Scratch space one build of this structure needs
     * @return The size in bytes
     */
    VkDeviceSize getBuildScratchSize() const { return m_scratchSize; }

    /**
     * @brief Get the acceleration structure handle
     * @return The handle, or VK_NULL_HANDLE if construction failed
     */
    VkAccelerationStructureKHR getAccelerationStructure() const { return m_accelerationStructure; }

    /**
     * @brief Get the device address of the acceleration structure
     * @return The device address, or 0 if construction failed
     */
    VkDeviceAddress getDeviceAddress() const { return m_deviceAddress; }

    /**
     * @brief Whether the acceleration structure has been built
     * @return True once build has completed
     */
    bool isBuilt() const { return m_isBuilt.load(std::memory_order_acquire); }

    /**
     * @brief Whether construction succeeded; an invalid BLAS must not be built or used
     * @return True if the acceleration structure was created
     */
    bool isValid() const { return m_isValid; }

  private:
    /**
     * @brief Create the acceleration structure and query its device address
     * @return True on success
     */
    bool createAccelerationStructure();

    /**
     * @brief Fill in the triangle geometry and build range from a mesh's buffers
     * @param mesh Mesh to read vertex and index allocations from
     * @return True on success
     */
    bool createGeometry(const Mesh &mesh);

  private:
    VkAccelerationStructureKHR m_accelerationStructure;
    VkAccelerationStructureGeometryKHR m_geometry;
    VkAccelerationStructureBuildGeometryInfoKHR m_buildInfo;
    VkAccelerationStructureBuildRangeInfoKHR m_buildRangeInfo;

    VkBuffer m_buffer;
    VmaAllocation m_allocation;

    VkAccelerationStructureKHR m_compactedAccelerationStructure;
    VkBuffer m_compactedBuffer;
    VmaAllocation m_compactedAllocation;

    VkDeviceAddress m_deviceAddress;
    VkDeviceSize m_accelerationStructureSize;
    VkDeviceSize m_scratchSize;

    std::atomic<bool> m_isBuilt;
    std::atomic<bool> m_isReady;
    bool m_isValid;

    VkDevice m_device;
    VmaAllocator m_allocator;
};

} // namespace Rapture

#endif // RAPTURE__BLAS_H
