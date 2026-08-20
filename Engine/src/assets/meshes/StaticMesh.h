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
};

} // namespace Rapture

#endif // RAPTURE__STATIC_MESH_H
