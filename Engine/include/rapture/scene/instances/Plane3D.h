#ifndef RAPTURE__PLANE3D_H
#define RAPTURE__PLANE3D_H

#include <rapture/scene/instances/PrimitiveMesh3D.h>

namespace Rapture {

/**
 * @brief A flat square facing up, one unit along each side before its transform scales it.
 */
class Plane3D : public PrimitiveMesh3D {
  public:
    Plane3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;
};

} // namespace Rapture

#endif // RAPTURE__PLANE3D_H
