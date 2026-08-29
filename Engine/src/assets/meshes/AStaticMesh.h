#ifndef RAPTURE__ASTATIC_MESH_H
#define RAPTURE__ASTATIC_MESH_H

#include "assets/meshes/AMesh.h"
#include "assets/meshes/StaticMesh.h"

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief A static mesh asset: geometry drawn with no deformation, and the material it defaults to
 */
class AStaticMesh : public AMesh {
  public:
    AStaticMesh(MeshAllocatorParams &params, AssetHandle defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE);
    AStaticMesh(StaticMesh geometry, AssetHandle defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    StaticMesh &geometry() { return m_geometry; }
    const StaticMesh &geometry() const { return m_geometry; }

    /**
     * @brief Serializes this asset, reading its geometry back off the GPU
     * @return The serialized bytes, empty if it holds no geometry
     */
    std::vector<uint8_t> serialize() const override;

    /**
     * @brief Serializes geometry that has not been uploaded yet, skipping the read back off the GPU
     * @param params The mesh data to write
     * @param defaultMaterial The material the asset defaults to
     * @return The serialized bytes
     */
    static std::vector<uint8_t> serializeParams(const MeshAllocatorParams &params, AssetHandle defaultMaterial);

    /**
     * @brief Builds a static mesh asset from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The asset, or nullptr if the blob is not a readable static mesh
     */
    static std::unique_ptr<AStaticMesh> deserialize(std::span<const uint8_t> blob);

  private:
    StaticMesh m_geometry;
};

} // namespace Rapture

#endif // RAPTURE__ASTATIC_MESH_H
