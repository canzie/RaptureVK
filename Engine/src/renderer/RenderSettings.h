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
    RENDER_SHOW_MOTION = 1 << 6,            // debug view: screen-space motion vectors
    RENDER_SHOW_AMBIENT_OCCLUSION = 1 << 7, // debug view: the traced occlusion term on its own
    RENDER_USE_AMBIENT_OCCLUSION = 1 << 8,  // modulate indirect diffuse by the traced occlusion
    RENDER_ALL = 0xFFFFFFFF,
};

/**
 * @brief Per-view, non-destructive render display overrides consumed by the renderer.
 */
struct RenderSettings {
    uint32_t flags = RENDER_USE_GLOBAL_ILLUMINATION | RENDER_SHOW_DIRECT | RENDER_SHOW_INDIRECT | RENDER_MODULATE_INDIRECT |
                     RENDER_USE_AMBIENT_OCCLUSION;

    bool useGlobalIllumination() const { return (flags & RENDER_USE_GLOBAL_ILLUMINATION) != 0u; }
    bool useDirectLighting() const { return (flags & RENDER_SHOW_DIRECT) != 0u; }
    bool useIndirectLighting() const { return (flags & RENDER_SHOW_INDIRECT) != 0u; }
    bool showDDGIProbes() const { return (flags & RENDER_SHOW_DDGI_PROBES) != 0u; }

    void setFlag(RenderSettingFlags flag, bool on) { flags = on ? (flags | flag) : (flags & ~flag); }
};

} // namespace Rapture

#endif // RAPTURE__RENDER_SETTINGS_H
