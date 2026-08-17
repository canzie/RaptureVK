#ifndef RAPTURE__STATICMESH3D_H
#define RAPTURE__STATICMESH3D_H

#include "scene/instances/Mesh3D.h"

namespace Rapture {

/**
 * @brief A mesh drawn with no deformation.
 */
class StaticMesh3D : public Mesh3D {
  public:
    StaticMesh3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void setMesh(AssetHandle mesh) override;

    bool isVisible() const override;
    void setVisible(bool visible) override;

    Mobility mobility() const override;
    void setMobility(Mobility mobility) override;

    glm::vec3 boundsMin() const override;
    glm::vec3 boundsMax() const override;

    void setRayTraced(bool rayTraced) override;

  private:
    /**
     * @brief Points this mesh's TLAS instance at the acceleration structure of the mesh it now holds,
     * dropping it out of ray tracing if that mesh has none
     */
    void rebuildAccelerationStructure();
};

} // namespace Rapture

#endif // RAPTURE__STATICMESH3D_H
