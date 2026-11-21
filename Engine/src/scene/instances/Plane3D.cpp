#include <rapture/scene/instances/Plane3D.h>

#include <rapture/assets/asset_manager/ReservedAssets.h>

namespace Rapture {

Plane3D::Plane3D(Scene &scene, std::string_view name) : PrimitiveMesh3D(scene, name, RE_PRIMITIVE_PLANE_MESH) {}

const TypeInfo &Plane3D::staticType()
{
    static const TypeInfo type("Plane3D", &PrimitiveMesh3D::staticType());
    return type;
}

const TypeInfo &Plane3D::type() const
{
    return staticType();
}

} // namespace Rapture
