#include "StaticMesh3D.h"

namespace Rapture {

StaticMesh3D::StaticMesh3D(Scene &scene, std::string_view name) : Mesh3D(scene, name) {}

const TypeInfo &StaticMesh3D::staticType()
{
    static const TypeInfo type("StaticMesh3D", &Mesh3D::staticType());
    return type;
}

const TypeInfo &StaticMesh3D::type() const
{
    return staticType();
}

} // namespace Rapture
