#include "AddSceneObjectMenu.h"

#include "EntitySelection.h"
#include "assets/asset_manager/ReservedAssets.h"
#include "layers/panels/components/context_menus.h"
#include "scene/Scene.h"
#include "scene/instances/Camera3D.h"
#include "scene/instances/DirectionalLight3D.h"
#include "scene/instances/Folder.h"
#include "scene/instances/Node3D.h"
#include "scene/instances/PointLight3D.h"
#include "scene/instances/SpotLight3D.h"
#include "scene/instances/SpringArm3D.h"
#include "scene/instances/StaticMesh3D.h"
#include "scene/instances/Terrain3D.h"

#include <components/context_menu_item.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

using MenuItem = std::unique_ptr<Amethyst::ContextMenu::ItemData>;

static void s_announce(Rapture::SceneObject *added, EntitySelection &selection)
{
    added->scene()->onHierarchyChanged.fire();
    selection.select(added->accessor());
}

static void s_append(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, std::string label,
                     std::function<void()> cb)
{
    if ((entryScopes & scope) == 0) {
        return;
    }
    items.push_back(Amethyst::makeActionItem(std::move(label), std::move(cb)));
}

/**
 * @brief Appends a mesh the user can drop in without picking an asset first
 */
static void s_appendPrimitive(std::vector<MenuItem> &items, SceneObjectScope scope, Rapture::SceneObject *parent,
                              EntitySelection &selection, std::string label, Rapture::AssetHandle mesh)
{
    std::string name = label;
    s_append(items, SCENE_OBJECT_SCOPE_ALL, scope, std::move(label), [parent, &selection, name, mesh]() {
        auto *node = parent->add<Rapture::StaticMesh3D>(name);
        node->setMesh(mesh);
        node->setMaterial(Rapture::RE_DEFAULT_MATERIAL_INSTANCE);
        s_announce(node, selection);
    });
}

/**
 * @brief Appends a class under a label of its own, constructed with nothing set on it
 */
template <typename T>
static void s_appendNamed(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, Rapture::SceneObject *parent,
                          EntitySelection &selection, std::string label, std::string_view name)
{
    s_append(items, entryScopes, scope, std::move(label),
             [parent, &selection, name]() { s_announce(parent->add<T>(name), selection); });
}

/**
 * @brief Appends a class under its own name, constructed with nothing set on it
 */
template <typename T>
static void s_appendClass(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, Rapture::SceneObject *parent,
                          EntitySelection &selection)
{
    s_appendNamed<T>(items, entryScopes, scope, parent, selection, std::string(T::staticType().name), T::staticType().name);
}

/**
 * @brief Nests a group under a label, dropping it when scope left it empty
 */
static void s_appendSubmenu(std::vector<MenuItem> &items, std::string label, std::vector<MenuItem> group,
                            std::function<void()> onActivate = {})
{
    if (group.empty()) {
        return;
    }
    items.push_back(Amethyst::makeSubmenuItem(std::move(label), std::move(group), std::move(onActivate)));
}

/**
 * @brief Nests a class's subclasses under it, with the row itself adding the class
 */
template <typename T>
static void s_appendClassSubmenu(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope,
                                 Rapture::SceneObject *parent, EntitySelection &selection, std::vector<MenuItem> subclasses)
{
    std::function<void()> onActivate;
    if ((entryScopes & scope) != 0) {
        onActivate = [parent, &selection]() { s_announce(parent->add<T>(T::staticType().name), selection); };
    }

    s_appendSubmenu(items, std::string(T::staticType().name), std::move(subclasses), std::move(onActivate));
}

/**
 * @brief Moves a group onto the menu under a section header, dropping both when the group is empty
 */
static void s_appendSection(std::vector<MenuItem> &items, std::string label, std::vector<MenuItem> group)
{
    if (group.empty()) {
        return;
    }

    items.push_back(ViewportContextMenuSID::create(std::move(label)));
    for (MenuItem &item : group) {
        items.push_back(std::move(item));
    }
}

/**
 * @brief The Node3D subtree, each class nested under the one it derives from
 */
static std::vector<MenuItem> s_buildNode3DItems(SceneObjectScope scope, Rapture::SceneObject *parent, EntitySelection &selection)
{
    std::vector<MenuItem> items;
    s_appendClass<Rapture::Camera3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendClass<Rapture::SpringArm3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendClass<Rapture::Terrain3D>(items, SCENE_OBJECT_SCOPE_LEVEL, scope, parent, selection);

    // Mesh3D and Light3D are not registered, so they open their subclasses without adding anything
    std::vector<MenuItem> meshes;
    s_appendClass<Rapture::StaticMesh3D>(meshes, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendSubmenu(items, "Mesh3D", std::move(meshes));

    std::vector<MenuItem> lights;
    s_appendClass<Rapture::DirectionalLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendClass<Rapture::PointLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendClass<Rapture::SpotLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection);
    s_appendSubmenu(items, "Light3D", std::move(lights));

    return items;
}

std::vector<MenuItem> AddSceneObjectMenu_buildItems(Rapture::SceneObject *parent, EntitySelection &selection, SceneObjectScope scope)
{
    std::vector<MenuItem> meshes;
    s_appendPrimitive(meshes, scope, parent, selection, "Cube", Rapture::RE_PRIMITIVE_CUBE_MESH);
    s_appendPrimitive(meshes, scope, parent, selection, "Sphere", Rapture::RE_PRIMITIVE_SPHERE_MESH);
    s_appendPrimitive(meshes, scope, parent, selection, "Plane", Rapture::RE_PRIMITIVE_PLANE_MESH);

    std::vector<MenuItem> lights;
    s_appendNamed<Rapture::DirectionalLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection, "Directional",
                                               "Directional Light");
    s_appendNamed<Rapture::PointLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection, "Point", "Point Light");
    s_appendNamed<Rapture::SpotLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection, "Spot", "Spot Light");

    std::vector<MenuItem> common;
    s_appendSubmenu(common, "Mesh", std::move(meshes));
    s_appendSubmenu(common, "Light", std::move(lights));
    s_appendClass<Rapture::Terrain3D>(common, SCENE_OBJECT_SCOPE_LEVEL, scope, parent, selection);

    std::vector<MenuItem> folders;
    s_appendClass<Rapture::Folder>(folders, SCENE_OBJECT_SCOPE_LEVEL, scope, parent, selection);

    std::vector<MenuItem> classes;
    s_appendClassSubmenu<Rapture::Node3D>(classes, SCENE_OBJECT_SCOPE_ALL, scope, parent, selection,
                                          s_buildNode3DItems(scope, parent, selection));

    std::vector<MenuItem> items;
    s_appendSection(items, "Common", std::move(common));
    s_appendSection(items, "Folder", std::move(folders));
    s_appendSection(items, "Advanced", std::move(classes));
    return items;
}
