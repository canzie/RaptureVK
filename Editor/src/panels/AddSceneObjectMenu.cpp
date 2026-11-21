#include "AddSceneObjectMenu.h"

#include "scene/EditedScene.h"
#include "ui/menus/ContextMenuItems.h"
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Box3D.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/Folder.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/Plane3D.h>
#include <rapture/scene/instances/PointLight3D.h>
#include <rapture/scene/instances/Sphere3D.h>
#include <rapture/scene/instances/SpotLight3D.h>
#include <rapture/scene/instances/SpringArm3D.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/Terrain3D.h>

#include <components/context_menu_item.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

using MenuItem = std::unique_ptr<Amethyst::ContextMenu::ItemData>;

static void s_append(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, std::string label,
                     std::function<void()> cb)
{
    if ((entryScopes & scope) == 0) {
        return;
    }
    items.push_back(Amethyst::makeActionItem(std::move(label), std::move(cb)));
}

/**
 * @brief Appends a class under a label of its own, constructed with nothing set on it
 */
template <typename T>
static void s_appendNamed(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, EditedScene &editedScene, Rapture::SceneObject &parent, std::string label, std::string_view name)
{
    s_append(items, entryScopes, scope, std::move(label),
             [&editedScene, &parent, name]() { editedScene.add<T>(parent, name); });
}

/**
 * @brief Appends a class under its own name, constructed with nothing set on it
 */
template <typename T>
static void s_appendClass(std::vector<MenuItem> &items, uint32_t entryScopes, SceneObjectScope scope, EditedScene &editedScene, Rapture::SceneObject &parent)
{
    s_appendNamed<T>(items, entryScopes, scope, editedScene, parent, std::string(T::staticType().name), T::staticType().name);
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
                                 EditedScene &editedScene, Rapture::SceneObject &parent, std::vector<MenuItem> subclasses)
{
    std::function<void()> onActivate;
    if ((entryScopes & scope) != 0) {
        onActivate = [&editedScene, &parent]() { editedScene.add<T>(parent, T::staticType().name); };
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

    items.push_back(SeparatorMenuItem::create(std::move(label)));
    for (MenuItem &item : group) {
        items.push_back(std::move(item));
    }
}

/**
 * @brief The Node3D subtree, each class nested under the one it derives from
 */
static std::vector<MenuItem> s_buildNode3DItems(SceneObjectScope scope, EditedScene &editedScene, Rapture::SceneObject &parent)
{
    std::vector<MenuItem> items;
    s_appendClass<Rapture::Camera3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::SpringArm3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::Terrain3D>(items, SCENE_OBJECT_SCOPE_LEVEL, scope, editedScene, parent);

    // Mesh3D and Light3D are not registered, so they open their subclasses without adding anything
    std::vector<MenuItem> primitives;
    s_appendClass<Rapture::Box3D>(primitives, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::Sphere3D>(primitives, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::Plane3D>(primitives, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);

    std::vector<MenuItem> meshes;
    s_appendClassSubmenu<Rapture::StaticMesh3D>(meshes, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, std::move(primitives));
    s_appendSubmenu(items, "Mesh3D", std::move(meshes));

    std::vector<MenuItem> lights;
    s_appendClass<Rapture::DirectionalLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::PointLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendClass<Rapture::SpotLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent);
    s_appendSubmenu(items, "Light3D", std::move(lights));

    return items;
}

/**
 * @brief The built-in shapes, each under its plain name
 */
static std::vector<MenuItem> s_buildShapeItems(SceneObjectScope scope, EditedScene &editedScene, Rapture::SceneObject &parent)
{
    std::vector<MenuItem> items;
    s_appendNamed<Rapture::Box3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Box", "Box");
    s_appendNamed<Rapture::Sphere3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Sphere", "Sphere");
    s_appendNamed<Rapture::Plane3D>(items, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Plane", "Plane");
    return items;
}

std::vector<MenuItem> AddSceneObjectMenu_buildItems(EditedScene &editedScene, Rapture::SceneObject &parent, SceneObjectScope scope)
{
    std::vector<MenuItem> meshes = s_buildShapeItems(scope, editedScene, parent);

    std::vector<MenuItem> lights;
    s_appendNamed<Rapture::DirectionalLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Directional",
                                               "Directional Light");
    s_appendNamed<Rapture::PointLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Point", "Point Light");
    s_appendNamed<Rapture::SpotLight3D>(lights, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent, "Spot", "Spot Light");

    std::vector<MenuItem> common;
    s_appendSubmenu(common, "Mesh", std::move(meshes));
    s_appendSubmenu(common, "Light", std::move(lights));
    s_appendClass<Rapture::Terrain3D>(common, SCENE_OBJECT_SCOPE_LEVEL, scope, editedScene, parent);

    std::vector<MenuItem> folders;
    s_appendClass<Rapture::Folder>(folders, SCENE_OBJECT_SCOPE_LEVEL, scope, editedScene, parent);

    std::vector<MenuItem> classes;
    s_appendClassSubmenu<Rapture::Node3D>(classes, SCENE_OBJECT_SCOPE_ALL, scope, editedScene, parent,
                                          s_buildNode3DItems(scope, editedScene, parent));

    std::vector<MenuItem> items;
    s_appendSection(items, "Common", std::move(common));
    s_appendSection(items, "Folder", std::move(folders));
    s_appendSection(items, "Advanced", std::move(classes));
    return items;
}

std::vector<MenuItem> AddSceneObjectMenu_buildShapeItems(EditedScene &editedScene, Rapture::SceneObject &parent, SceneObjectScope scope)
{
    return s_buildShapeItems(scope, editedScene, parent);
}
