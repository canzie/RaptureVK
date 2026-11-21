#ifndef RAPTURE__BOX3D_H
#define RAPTURE__BOX3D_H

#include <rapture/scene/instances/PrimitiveMesh3D.h>

namespace Rapture {

/**
 * @brief A box, one unit along each side before its transform scales it.
 */
class Box3D : public PrimitiveMesh3D {
  public:
    Box3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;
};

} // namespace Rapture

#endif // RAPTURE__BOX3D_H
