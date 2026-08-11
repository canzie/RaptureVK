#ifndef RAPTURE__CHANGECHANNELS_H
#define RAPTURE__CHANGECHANNELS_H

namespace Rapture {

/**
 * @brief What a change invalidates, named by cause rather than by who consumes it.
 */
enum SceneChannel {
    CHANNEL_TRANSFORM_WORLD,
    CHANNEL_MESH_BINDING,
    CHANNEL_MATERIAL_BINDING,
    CHANNEL_LIGHT_PARAMS,
    CHANNEL_SHADOW_SETTINGS,
    CHANNEL_CAMERA_PARAMS,
    CHANNEL_VISIBILITY,
    CHANNEL_COUNT
};

} // namespace Rapture

#endif // RAPTURE__CHANGECHANNELS_H
