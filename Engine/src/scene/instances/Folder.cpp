#include "Folder.h"

namespace Rapture {

Folder::Folder(Scene &scene, std::string_view name) : SceneObject(scene, name) {}

const TypeInfo &Folder::staticType()
{
    static const TypeInfo type("Folder", &SceneObject::staticType());
    return type;
}

const TypeInfo &Folder::type() const
{
    return staticType();
}

} // namespace Rapture
