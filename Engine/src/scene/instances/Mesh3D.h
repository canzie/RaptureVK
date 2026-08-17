#ifndef RAPTURE__MESH3D_H
#define RAPTURE__MESH3D_H

#include "assets/asset_manager/AssetCommon.h"
#include "scene/EntityCommon.h"
#include "scene/instances/Node3D.h"

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
    virtual void setMesh(AssetHandle mesh) = 0;

    AssetHandle material() const { return m_material; }
    void setMaterial(AssetHandle material);

    virtual bool isVisible() const = 0;
    virtual void setVisible(bool visible) = 0;

    virtual Mobility mobility() const = 0;
    virtual void setMobility(Mobility mobility) = 0;

    virtual glm::vec3 boundsMin() const = 0;
    virtual glm::vec3 boundsMax() const = 0;

    bool isRayTraced() const;

    /**
     * @brief Builds or drops the acceleration structure this mesh contributes to the scene TLAS
     * @param rayTraced Whether the mesh takes part in ray tracing
     */
    virtual void setRayTraced(bool rayTraced) = 0;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    Mesh3D(Scene &scene, std::string_view name);

  protected:
    AssetHandle m_mesh = INVALID_ASSET_HANDLE;

  private:
    AssetHandle m_material = INVALID_ASSET_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__MESH3D_H
