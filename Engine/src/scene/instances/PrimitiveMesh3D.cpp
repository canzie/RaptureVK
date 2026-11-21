#include <rapture/scene/instances/PrimitiveMesh3D.h>

namespace Rapture {

PrimitiveMesh3D::PrimitiveMesh3D(Scene &scene, std::string_view name, AssetHandle shape) : StaticMesh3D(scene, name)
{
    setMesh(shape);
}

const TypeInfo &PrimitiveMesh3D::staticType()
{
    static const TypeInfo type("PrimitiveMesh3D", &StaticMesh3D::staticType());
    return type;
}

const TypeInfo &PrimitiveMesh3D::type() const
{
    return staticType();
}

} // namespace Rapture
