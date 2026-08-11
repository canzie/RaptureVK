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
    ~Mesh3D() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle mesh() const { return m_mesh; }
    void setMesh(AssetHandle mesh);

    AssetHandle material() const { return m_material; }
    void setMaterial(AssetHandle material);

    bool isVisible() const;
    void setVisible(bool visible);

    Mobility mobility() const;
    void setMobility(Mobility mobility);

    glm::vec3 boundsMin() const;
    glm::vec3 boundsMax() const;

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

  private:
    AssetHandle m_mesh = INVALID_ASSET_HANDLE;
    AssetHandle m_material = INVALID_ASSET_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__MESH3D_H
