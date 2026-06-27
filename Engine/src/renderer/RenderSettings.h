#ifndef RAPTURE__RENDER_SETTINGS_H
#define RAPTURE__RENDER_SETTINGS_H

namespace Rapture {

/**
 * @brief Per-view, non-destructive render display overrides consumed by the renderer.
 */
struct RenderSettings {
    bool useGI = true;
};

} // namespace Rapture

#endif // RAPTURE__RENDER_SETTINGS_H
