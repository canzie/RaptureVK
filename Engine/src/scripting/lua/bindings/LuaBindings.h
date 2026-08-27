#ifndef RAPTURE__LUABINDINGS_H
#define RAPTURE__LUABINDINGS_H

#include <sol/sol.hpp>

namespace Rapture::scripting {

/**
 * @brief Puts Vector3 and Quat into a state
 * @param lua The state to bind into
 */
void registerMathBindings(sol::state_view lua);

/**
 * @brief Puts Signal and Connection into a state
 * @param lua The state to bind into
 */
void registerEventSignalBindings(sol::state_view lua);

/**
 * @brief Puts Instance, SceneObject and Folder into a state
 * @param lua The state to bind into
 */
void registerInstanceBindings(sol::state_view lua);

/**
 * @brief Puts the classes with a place in the world into a state
 * @param lua The state to bind into
 */
void registerNode3DBindings(sol::state_view lua);

/**
 * @brief Puts the controller classes into a state
 * @param lua The state to bind into
 */
void registerControllerBindings(sol::state_view lua);

/**
 * @brief Puts the scene component classes into a state
 * @param lua The state to bind into
 */
void registerSceneComponentBindings(sol::state_view lua);

} // namespace Rapture::scripting

#endif // RAPTURE__LUABINDINGS_H
