// RenderFlags.glsl - the per-view display overrides
// The C++ RenderSettingFlags enum in renderer/RenderSettings.h must match this exactly.

#ifndef RENDER_FLAGS_GLSL
#define RENDER_FLAGS_GLSL

const uint RENDER_NONE = 0u;
const uint RENDER_USE_GLOBAL_ILLUMINATION = 1u << 0;
const uint RENDER_SHOW_DIRECT = 1u << 1;
const uint RENDER_SHOW_INDIRECT = 1u << 2;
const uint RENDER_MODULATE_INDIRECT = 1u << 3;
const uint RENDER_SHOW_DDGI_PROBES = 1u << 4;
const uint RENDER_SHOW_NORMALS = 1u << 5;
const uint RENDER_SHOW_MOTION = 1u << 6;
const uint RENDER_SHOW_SSSR_HIT = 1u << 7;
const uint RENDER_SHOW_SSSR_RESOLVED = 1u << 8;
const uint RENDER_SHOW_SSSR_ACCUMULATED = 1u << 9;
const uint RENDER_SHOW_SSSR_CONFIDENCE = 1u << 10;
const uint RENDER_USE_SCREEN_SPACE_REFLECTIONS = 1u << 11;
const uint RENDER_USE_AMBIENT_OCCLUSION = 1u << 12;
const uint RENDER_SHOW_AMBIENT_OCCLUSION = 1u << 13;
const uint RENDER_SHOW_BENT_NORMALS = 1u << 14;
const uint RENDER_ALL = 0xFFFFFFFFu;

#endif // RENDER_FLAGS_GLSL
