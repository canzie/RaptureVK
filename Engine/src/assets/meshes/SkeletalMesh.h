#ifndef RAPTURE__SKELETAL_MESH_H
#define RAPTURE__SKELETAL_MESH_H

#include "assets/asset_manager/AssetCommon.h"
#include "assets/meshes/Mesh.h"

namespace Rapture {

/**
 * @brief Geometry bound to a skeleton
 *
 * The binding is recorded at import rather than chosen, since a mesh's joint indices address one
 * skeleton's joint list and no other.
 */
class SkeletalMesh : public Mesh {
  public:
    SkeletalMesh(MeshAllocatorParams &params, AssetHandle skeleton, std::vector<glm::mat4> inverseBindMatrices);
    SkeletalMesh() = default;

    AssetHandle getSkeleton() const { return m_skeleton; }

    /**
     * @brief The bind time global transform of each joint, inverted
     *
     * Takes a vertex from mesh space into a joint's frame as it stood when this mesh was authored,
     * which is what lets the joint's current transform be applied to it.
     */
    const std::vector<glm::mat4> &getInverseBindMatrices() const { return m_inverseBindMatrices; }

    uint32_t getJointCount() const { return static_cast<uint32_t>(m_inverseBindMatrices.size()); }

    /**
     * @brief Serializes this mesh, reading its geometry back off the GPU
     * @return The serialized bytes, empty if this mesh holds no geometry
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Serializes mesh data that has not been uploaded yet, skipping the read back off the GPU
     * @param params The mesh data to write
     * @param skeleton The skeleton the mesh is bound to
     * @param inverseBindMatrices One matrix per joint
     * @return The serialized bytes
     */
    static std::vector<uint8_t> serializeParams(const MeshAllocatorParams &params, AssetHandle skeleton,
                                                const std::vector<glm::mat4> &inverseBindMatrices);

    /**
     * @brief Builds a skeletal mesh from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The mesh, or nullptr if the blob is not a readable skeletal mesh
     */
    static std::unique_ptr<SkeletalMesh> deserialize(std::span<const uint8_t> blob);

  private:
    AssetHandle m_skeleton = INVALID_ASSET_HANDLE;
    std::vector<glm::mat4> m_inverseBindMatrices;
};

} // namespace Rapture

#endif // RAPTURE__SKELETAL_MESH_H
