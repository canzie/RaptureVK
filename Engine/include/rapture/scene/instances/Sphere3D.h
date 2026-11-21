#ifndef RAPTURE__SPHERE3D_H
#define RAPTURE__SPHERE3D_H

#include <rapture/scene/instances/PrimitiveMesh3D.h>

namespace Rapture {

/**
 * @brief A sphere, one unit in radius before its transform scales it.
 */
class Sphere3D : public PrimitiveMesh3D {
  public:
    Sphere3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;
};

} // namespace Rapture

#endif // RAPTURE__SPHERE3D_H
