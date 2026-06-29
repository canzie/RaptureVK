# DDGI

**Source: `Engine/src/renderer/gi/ddgi/DynamicDiffuseGI.h/.cpp`**

Probe-based diffuse global illumination using GPU ray tracing. Runs as a compute pass each frame, producing irradiance and visibility textures consumed by [[LightingPass]]. Uses [[DescriptorManager]] Set 4 for probe-specific bindings.
