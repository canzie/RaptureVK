#include <rapture/scene/instances/Box3D.h>

#include <rapture/assets/asset_manager/ReservedAssets.h>

namespace Rapture {

Box3D::Box3D(Scene &scene, std::string_view name) : PrimitiveMesh3D(scene, name, RE_PRIMITIVE_CUBE_MESH) {}

const TypeInfo &Box3D::staticType()
{
    static const TypeInfo type("Box3D", &PrimitiveMesh3D::staticType());
    return type;
}

const TypeInfo &Box3D::type() const
{
    return staticType();
}

} // namespace Rapture
