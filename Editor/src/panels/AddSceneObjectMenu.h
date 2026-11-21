#ifndef RAPTURE__ADD_SCENE_OBJECT_MENU_H
#define RAPTURE__ADD_SCENE_OBJECT_MENU_H

#include <components/context_menu.h>

#include <memory>
#include <vector>

namespace Rapture {
class SceneObject;
}

class EditedScene;

/**
 * @brief Where a scene object may be added, one bit per place a menu is built for
 */
enum SceneObjectScope {
    SCENE_OBJECT_SCOPE_LEVEL = 1 << 0,
    SCENE_OBJECT_SCOPE_ASSET = 1 << 1,
    SCENE_OBJECT_SCOPE_ALL = SCENE_OBJECT_SCOPE_LEVEL | SCENE_OBJECT_SCOPE_ASSET
};

/**
 * @brief Builds the context menu items that create a scene object, each adding its object under a parent
 * @param editedScene The scene the objects are added to
 * @param parent The object new objects are added under
 * @param scope The scope the menu is built for, which admits the entries carrying its bit
 * @return The items, ready to hand to a ContextMenu
 */
std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>>
AddSceneObjectMenu_buildItems(EditedScene &editedScene, Rapture::SceneObject &parent, SceneObjectScope scope);

/**
 * @brief Builds the context menu items that create one of the built-in shapes under a parent
 * @param editedScene The scene the shapes are added to
 * @param parent The object new shapes are added under
 * @param scope The scope the menu is built for
 * @return The items, ready to hand to a ContextMenu
 */
std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>>
AddSceneObjectMenu_buildShapeItems(EditedScene &editedScene, Rapture::SceneObject &parent, SceneObjectScope scope);

#endif // RAPTURE__ADD_SCENE_OBJECT_MENU_H
