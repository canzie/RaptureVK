#ifndef RAPTURE__BLAS_H
#define RAPTURE__BLAS_H

#include "gpu/buffers/Buffers.h"

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
     * @brief Record and submit the acceleration structure build
     */
    void build();

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
    bool isBuilt() const { return m_isBuilt; }

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

    VkBuffer m_scratchBuffer;
    VmaAllocation m_scratchAllocation;

    VkDeviceAddress m_deviceAddress;
    VkDeviceSize m_accelerationStructureSize;
    VkDeviceSize m_scratchSize;

    bool m_isBuilt;
    bool m_isValid;

    VkDevice m_device;
    VmaAllocator m_allocator;
};

} // namespace Rapture

#endif // RAPTURE__BLAS_H
