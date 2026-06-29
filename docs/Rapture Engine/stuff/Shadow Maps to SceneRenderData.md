# Shadow Maps to SceneRenderData

Status: **TODO**
Related: [[SceneRenderData]], [[SceneRenderData Implementation]]

## Problem

ShadowComponent and CascadedShadowComponent still require `RenderContext` because they create shadow map textures in their constructors. This is the last remaining coupling after buffers were fully moved to SceneRenderData.

Components should be pure config data — users shouldn't need GPU infrastructure knowledge to add a shadow.

## Solution

SceneRenderData already has callback infrastructure and access to RenderContext. Shadow map textures should be created in SceneRenderData callbacks, same pattern as the buffer work.

- Component stores config only (resolution, cascade count, lambda, etc)
- SceneRenderData owns the texture lifetime
- entt `on_construct` signal → SignalBridge → create GPU resource

## Scope

- **ShadowComponent** — shadow map texture created via SceneRenderData callback on component add
- **CascadedShadowComponent** — cascaded shadow map textures created the same way

## Result

Eliminates the last RenderContext leak into component APIs. Components become headless-safe, "components are data" principle fully intact.
