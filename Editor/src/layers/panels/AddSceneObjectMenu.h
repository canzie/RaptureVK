#ifndef RAPTURE__ADD_SCENE_OBJECT_MENU_H
#define RAPTURE__ADD_SCENE_OBJECT_MENU_H

#include <components/context_menu.h>

#include <memory>
#include <vector>

namespace Rapture {
class Instance;
}

class EntitySelection;

/**
 * @brief Where a scene object may be added, one bit per place a menu is built for
 */
enum SceneObjectScope {
    SCENE_OBJECT_SCOPE_LEVEL = 1 << 0,
    SCENE_OBJECT_SCOPE_MODULE = 1 << 1,
    SCENE_OBJECT_SCOPE_ALL = SCENE_OBJECT_SCOPE_LEVEL | SCENE_OBJECT_SCOPE_MODULE
};

/**
 * @brief Builds the context menu items that create a scene object, each adding its object under a parent
 * @param parent The scene object new objects are parented to
 * @param selection The selection the newly added object becomes
 * @param scope The scope the menu is built for, which admits the entries carrying its bit
 * @return The items, ready to hand to a ContextMenu
 */
std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>>
AddSceneObjectMenu_buildItems(Rapture::Instance *parent, EntitySelection &selection, SceneObjectScope scope);

#endif // RAPTURE__ADD_SCENE_OBJECT_MENU_H
