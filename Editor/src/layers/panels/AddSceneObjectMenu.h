#ifndef RAPTURE__ADD_SCENE_OBJECT_MENU_H
#define RAPTURE__ADD_SCENE_OBJECT_MENU_H

#include <components/context_menu.h>

#include <memory>
#include <vector>

namespace Rapture {
class Instance;
}

/**
 * @brief The context menu items that create a scene object, shared by the hotbar and the outliner.
 */
namespace AddSceneObjectMenu {

/**
 * @brief Builds the menu items, each adding its object under a parent
 * @param parent The scene object new objects are parented to
 * @return The items, ready to hand to a ContextMenu
 */
std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> buildItems(Rapture::Instance *parent);

} // namespace AddSceneObjectMenu

#endif // RAPTURE__ADD_SCENE_OBJECT_MENU_H
