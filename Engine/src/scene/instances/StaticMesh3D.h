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
};

} // namespace Rapture

#endif // RAPTURE__STATICMESH3D_H
