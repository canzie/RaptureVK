#ifndef RAPTURE__ASTATIC_MESH_H
#define RAPTURE__ASTATIC_MESH_H

#include "assets/meshes/AMesh.h"
#include "assets/meshes/StaticMesh.h"

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief A static mesh asset: geometry drawn with no deformation, and the materials its runs default to
 */
class AStaticMesh : public AMesh {
  public:
    AStaticMesh(MeshAllocatorParams &params, std::vector<AssetHandle> materialSlots = {});
    AStaticMesh(StaticMesh geometry, std::vector<AssetHandle> materialSlots = {});

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
     * @param materialSlots The materials the asset's runs default to
     * @return The serialized bytes
     */
    static std::vector<uint8_t> serializeParams(const MeshAllocatorParams &params, const std::vector<AssetHandle> &materialSlots);

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
