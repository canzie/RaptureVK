# MaterialInstance

**Source: `Engine/src/assets/materials/MaterialInstance.h/.cpp`**

Per-entity material instance overriding parameters of a [[Material|BaseMaterial]]. Backed by a [[UniformBuffer]] and registered bindless for GPU access. Supports texture and scalar parameters with type-safe get/set and deferred texture loading.

Publishes `onMaterialInstanceChanged` events via [[AssetEvents]].
