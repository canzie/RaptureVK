#ifndef RAPTURE__PRIMITIVEMESH3D_H
#define RAPTURE__PRIMITIVEMESH3D_H

#include <rapture/scene/instances/StaticMesh3D.h>

namespace Rapture {

/**
 * @brief A static mesh drawing one of the built-in shapes, which shape being fixed by its class.
 */
class PrimitiveMesh3D : public StaticMesh3D {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

  protected:
    /**
     * @brief Creates the object drawing a built-in shape
     * @param scene The scene it belongs to
     * @param name Its name
     * @param shape The built-in mesh of the shape it draws
     */
    PrimitiveMesh3D(Scene &scene, std::string_view name, AssetHandle shape);
};

} // namespace Rapture

#endif // RAPTURE__PRIMITIVEMESH3D_H
