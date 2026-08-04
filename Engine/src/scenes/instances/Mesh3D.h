#ifndef RAPTURE__MESH3D_H
#define RAPTURE__MESH3D_H

#include "asset_manager/AssetCommon.h"
#include "scenes/entities/EntityCommon.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief Shared surface of everything drawn from a mesh and a material.
 */
class Mesh3D : public Node3D {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle mesh() const;
    void setMesh(AssetHandle mesh);

    AssetHandle material() const;
    void setMaterial(AssetHandle material);

    bool isVisible() const;
    void setVisible(bool visible);

    Mobility mobility() const;
    void setMobility(Mobility mobility);

    glm::vec3 boundsMin() const;
    glm::vec3 boundsMax() const;

    /**
     * @brief Sets the local bounds used for culling and for bounds derived physics shapes
     * @param min Lower corner in local space
     * @param max Upper corner in local space
     */
    void setBounds(const glm::vec3 &min, const glm::vec3 &max);

    bool isRayTraced() const;

    /**
     * @brief Builds or drops the acceleration structure this mesh contributes to the scene TLAS
     * @param rayTraced Whether the mesh takes part in ray tracing
     */
    void setRayTraced(bool rayTraced);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    Mesh3D(Scene &scene, std::string_view name);
};

} // namespace Rapture

#endif // RAPTURE__MESH3D_H
