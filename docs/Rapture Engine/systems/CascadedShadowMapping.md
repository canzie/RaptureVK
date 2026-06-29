# CascadedShadowMapping

**Source: `Engine/src/renderer/shadows/CascadedShadowMapping.h/.cpp`**

Cascaded shadow map implementation. Renders shadow maps into a texture array using multiple cascades. Uses [[MDI Batching]] for entity draws and supports terrain shadow rendering. Provides bindless texture access and cascade split calculation.
