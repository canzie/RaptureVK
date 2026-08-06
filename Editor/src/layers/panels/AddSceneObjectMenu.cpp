#include "AddSceneObjectMenu.h"

#include "asset_manager/ReservedAssets.h"
#include "events/GameEvents.h"
#include "scenes/Scene.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Folder.h"
#include "scenes/instances/PointLight3D.h"
#include "scenes/instances/SpotLight3D.h"
#include "scenes/instances/StaticMesh3D.h"

#include <components/context_menu_item.h>

static void s_announce(Rapture::Instance *added)
{
    added->scene()->onHierarchyChanged.fire();
    Rapture::GameEvents::onEntitySelected().publish(added->entity());
}

static void s_addMesh(Rapture::Instance *parent, std::string_view name, Rapture::AssetHandle mesh)
{
    auto *node = parent->add<Rapture::StaticMesh3D>(name);
    node->setMesh(mesh);
    node->setMaterial(Rapture::RE_DEFAULT_MATERIAL_INSTANCE);
    s_announce(node);
}

template <typename T>
static void s_addObject(Rapture::Instance *parent, std::string_view name)
{
    s_announce(parent->add<T>(name));
}

namespace AddSceneObjectMenu {

std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> buildItems(Rapture::Instance *parent)
{
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> meshes;
    meshes.push_back(
        Amethyst::makeActionItem("Cube", [parent]() { s_addMesh(parent, "Cube", Rapture::RE_PRIMITIVE_CUBE_MESH); }));
    meshes.push_back(
        Amethyst::makeActionItem("Sphere", [parent]() { s_addMesh(parent, "Sphere", Rapture::RE_PRIMITIVE_SPHERE_MESH); }));
    meshes.push_back(
        Amethyst::makeActionItem("Plane", [parent]() { s_addMesh(parent, "Plane", Rapture::RE_PRIMITIVE_PLANE_MESH); }));

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> lights;
    lights.push_back(Amethyst::makeActionItem(
        "Directional", [parent]() { s_addObject<Rapture::DirectionalLight3D>(parent, "Directional Light"); }));
    lights.push_back(
        Amethyst::makeActionItem("Point", [parent]() { s_addObject<Rapture::PointLight3D>(parent, "Point Light"); }));
    lights.push_back(
        Amethyst::makeActionItem("Spot", [parent]() { s_addObject<Rapture::SpotLight3D>(parent, "Spot Light"); }));

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(Amethyst::makeSubmenuItem("Mesh", std::move(meshes)));
    items.push_back(Amethyst::makeSubmenuItem("Light", std::move(lights)));
    items.push_back(Amethyst::makeSeparatorItem());
    items.push_back(Amethyst::makeActionItem("Folder", [parent]() { s_addObject<Rapture::Folder>(parent, "Folder"); }));
    return items;
}

} // namespace AddSceneObjectMenu
