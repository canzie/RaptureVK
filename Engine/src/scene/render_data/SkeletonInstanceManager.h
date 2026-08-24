#ifndef RAPTURE__SKELETON_INSTANCE_MANAGER_H
#define RAPTURE__SKELETON_INSTANCE_MANAGER_H

#include "assets/asset_manager/AssetCommon.h"
#include "assets/asset_manager/AssetHandle.h"
#include "gpu/buffers/VirtualStorageBuffer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace Rapture {

class ASkeleton;
class ASkeletalMesh;

/**
 * @brief One skeleton's worth of bone matrices, given back to the arena when it goes
 *
 * Does not outlive the SkeletonInstanceManager it came from.
 */
class SkeletonInstance {
  public:
    SkeletonInstance(VirtualStorageBuffer &buffer, uint32_t jointCount);
    ~SkeletonInstance();

    SkeletonInstance(const SkeletonInstance &) = delete;
    SkeletonInstance &operator=(const SkeletonInstance &) = delete;
    SkeletonInstance(SkeletonInstance &&other) noexcept;
    SkeletonInstance &operator=(SkeletonInstance &&other) noexcept;

    /**
     * @brief Where a shader reads this instance's first bone from
     */
    uint32_t getBoneOffset() const;

    uint32_t getJointCount() const;

    /**
     * @brief Upload this instance's bone matrices
     * @param matrices One matrix per joint, in the skeleton's joint order
     */
    void write(std::span<const glm::mat4> matrices);

  private:
    VirtualStorageBuffer *m_buffer = nullptr;
    VirtualStorageBuffer::Allocation m_allocation;
};

/**
 * @brief Owns the arena a scene's skeleton instances keep their bone matrices in
 */
class SkeletonInstanceManager {
  public:
    SkeletonInstanceManager();
    ~SkeletonInstanceManager();

    SkeletonInstanceManager(const SkeletonInstanceManager &) = delete;
    SkeletonInstanceManager &operator=(const SkeletonInstanceManager &) = delete;

    /**
     * @brief Make an instance of a skeleton, with room for its bones
     * @param skeleton The skeleton to be posed
     * @return The instance
     */
    SkeletonInstance createSkeletonInstance(const AssetPtr<ASkeleton> &skeleton);

    /**
     * @brief Where a shader reads a mesh's inverse bind matrices from
     *
     * The matrices are the same for every instance of a mesh, so the first one to ask puts them in
     * the arena and the rest are given what is already there.
     *
     * @param mesh The mesh bound to a skeleton
     * @return Where its first inverse bind sits, or SKIN_NO_OFFSET if the arena had no room
     */
    uint32_t getInverseBindOffset(const AssetPtr<ASkeletalMesh> &mesh);

    /**
     * @brief The index a shader reads this scene's bone matrices from
     */
    uint32_t getBindlessIndex() const { return m_bindlessIndex; }

  private:
    std::unique_ptr<VirtualStorageBuffer> m_buffer;
    std::unordered_map<AssetHandle, VirtualStorageBuffer::Allocation> m_inverseBinds;
    uint32_t m_bindlessIndex = UINT32_MAX;
};

} // namespace Rapture

#endif // RAPTURE__SKELETON_INSTANCE_MANAGER_H
