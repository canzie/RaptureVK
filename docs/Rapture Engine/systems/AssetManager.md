# AssetManager

**Source: `Engine/src/asset_manager/`**

Static singleton for asset lifecycle and import pipeline. `AssetImporter` handles per-type loading (shaders, materials, textures, cubemaps, scenes) by registered function dispatch.

Two manager implementations:
- **`AssetManagerEditor`** (active) — full import pipeline. Loads raw source formats: glTF scenes, PNG/JPG textures, GLSL shaders, material definitions. Supports virtual asset registration (runtime-created assets).
- **`AssetManagerRuntime`** (planned) — production builds. Loads only pre-baked internal asset representations (no raw format support). Optimized for fast loading with no import overhead.

Provides [[AssetRef]] for reference-counted access and [[AssetHandle]] (UUID) for identification.
