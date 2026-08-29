#ifndef RAPTURE__ASKELETAL_MESH_H
#define RAPTURE__ASKELETAL_MESH_H

#include "assets/meshes/AMesh.h"
#include "assets/meshes/SkeletalMesh.h"

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief A skeletal mesh asset: geometry deformed by a skeleton, and the material it defaults to
 */
class ASkeletalMesh : public AMesh {
  public:
    ASkeletalMesh(MeshAllocatorParams &params, AssetHandle skeleton, std::vector<glm::mat4> inverseBindMatrices,
                  AssetHandle defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    SkeletalMesh &geometry() { return m_geometry; }
    const SkeletalMesh &geometry() const { return m_geometry; }

    /**
     * @brief The skeleton this mesh's joint indices address
     * @return The skeleton asset
     */
    AssetHandle skeleton() const { return m_geometry.getSkeleton(); }

    /**
     * @brief Serializes this asset, reading its geometry back off the GPU
     * @return The serialized bytes, empty if it holds no geometry
     */
    std::vector<uint8_t> serialize() const override;

    /**
     * @brief Serializes geometry that has not been uploaded yet, skipping the read back off the GPU
     * @param params The mesh data to write
     * @param skeleton The skeleton the geometry is bound to
     * @param inverseBindMatrices One matrix per joint
     * @param defaultMaterial The material the asset defaults to
     * @return The serialized bytes
     */
    static std::vector<uint8_t> serializeParams(const MeshAllocatorParams &params, AssetHandle skeleton,
                                                const std::vector<glm::mat4> &inverseBindMatrices, AssetHandle defaultMaterial);

    /**
     * @brief Builds a skeletal mesh asset from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The asset, or nullptr if the blob is not a readable skeletal mesh
     */
    static std::unique_ptr<ASkeletalMesh> deserialize(std::span<const uint8_t> blob);

  private:
    SkeletalMesh m_geometry;
};

} // namespace Rapture

#endif // RAPTURE__ASKELETAL_MESH_H
