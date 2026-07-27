#ifndef RAPTURE__RENDER_SETTINGS_H
#define RAPTURE__RENDER_SETTINGS_H

#include <cstdint>

namespace Rapture {

/**
 * @brief Bit flags packed into RenderSettings::flags.
 */
enum RenderSettingFlags {
    RENDER_NONE = 0,
    RENDER_USE_GLOBAL_ILLUMINATION = 1 << 0, // run the global illumination system
    RENDER_SHOW_DIRECT = 1 << 1,             // direct analytic light (Lo)
    RENDER_SHOW_INDIRECT = 1 << 2,           // indirect diffuse
    RENDER_MODULATE_INDIRECT = 1 << 3,       // modulate indirect by albedo/ao (off = raw irradiance)
    RENDER_SHOW_DDGI_PROBES = 1 << 4,        // debug overlay: draw DDGI probe spheres
    RENDER_SHOW_NORMALS = 1 << 5,
    RENDER_ALL = 0xFFFFFFFF,
};

/**
 * @brief Per-view, non-destructive render display overrides consumed by the renderer.
 */
struct RenderSettings {
    uint32_t flags = RENDER_ALL & ~RENDER_SHOW_DDGI_PROBES & ~RENDER_SHOW_NORMALS;

    bool useGlobalIllumination() const { return (flags & RENDER_USE_GLOBAL_ILLUMINATION) != 0u; }
    bool useDirectLighting() const { return (flags & RENDER_SHOW_DIRECT) != 0u; }
    bool useIndirectLighting() const { return (flags & RENDER_SHOW_INDIRECT) != 0u; }
    bool showDDGIProbes() const { return (flags & RENDER_SHOW_DDGI_PROBES) != 0u; }

    void setFlag(RenderSettingFlags flag, bool on) { flags = on ? (flags | flag) : (flags & ~flag); }
};

} // namespace Rapture

#endif // RAPTURE__RENDER_SETTINGS_H
