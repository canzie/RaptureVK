#include <rapture/scene/instances/Sphere3D.h>

#include <rapture/assets/asset_manager/ReservedAssets.h>

namespace Rapture {

Sphere3D::Sphere3D(Scene &scene, std::string_view name) : PrimitiveMesh3D(scene, name, RE_PRIMITIVE_SPHERE_MESH) {}

const TypeInfo &Sphere3D::staticType()
{
    static const TypeInfo type("Sphere3D", &PrimitiveMesh3D::staticType());
    return type;
}

const TypeInfo &Sphere3D::type() const
{
    return staticType();
}

} // namespace Rapture
