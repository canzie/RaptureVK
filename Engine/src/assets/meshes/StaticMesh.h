#ifndef RAPTURE__STATIC_MESH_H
#define RAPTURE__STATIC_MESH_H

#include "assets/meshes/Mesh.h"

namespace Rapture {

/**
 * @brief Geometry drawn with no deformation, carrying no joint or weight attributes
 */
class StaticMesh : public Mesh {
  public:
    explicit StaticMesh(MeshAllocatorParams &params);
    StaticMesh() = default;

    /**
     * @brief Serializes this mesh, reading its geometry back off the GPU
     * @return The serialized bytes, empty if this mesh holds no geometry
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Serializes mesh data that has not been uploaded yet, skipping the read back off the GPU
     * @param params The mesh data to write
     * @return The serialized bytes
     */
    static std::vector<uint8_t> serializeParams(const MeshAllocatorParams &params);

    /**
     * @brief Builds a static mesh from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The mesh, or nullptr if the blob is not a readable static mesh
     */
    static std::unique_ptr<StaticMesh> deserialize(std::span<const uint8_t> blob);
};

} // namespace Rapture

#endif // RAPTURE__STATIC_MESH_H
