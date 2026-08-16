#ifndef RAPTURE__SHADER_EVENTS_H
#define RAPTURE__SHADER_EVENTS_H

#include "Events.h"

#include <string_view>

namespace Rapture {
namespace ShaderEvents {

using ShaderSourceChangedEvent = EventBus<std::string_view /*fileName*/>;

/**
 * @brief Fired with the file name of a shader source that was regenerated, so shaders that include
 *        it can recompile
 * @return The event bus
 */
inline ShaderSourceChangedEvent &onShaderSourceChanged()
{
    return EventRegistry::getInstance().getEventBus<std::string_view>("ShaderSourceChanged");
}

} // namespace ShaderEvents
} // namespace Rapture

#endif // RAPTURE__SHADER_EVENTS_H
