# Asset Management System

This document outlines the architecture of the engine's asset management system, a robust, multithreaded pipeline designed for high-performance loading and organized handling of game assets.

## System Overview

The asset manager is responsible for the entire lifecycle of assets, from their initial import to their use at runtime. It is built around a few core concepts:

-   **Asset**: An in-memory representation of game data, such as a mesh, texture, or material. All assets are derived from a common `Asset` base class and are identified by a unique `AssetHandle` (a UUID).
-   **Asset Metadata**: Information about an asset, including its handle, type, file path, and whether its data is currently loaded into memory.
-   **Asset Registry**: A central catalog that maps every `AssetHandle` to its corresponding `AssetMetadata`. This registry acts as a manifest of all available assets for the project.



### Editor vs. Standalone Game Asset Management

The system features two distinct asset manager implementations tailored for different stages of the production pipeline:

1.  **`AssetManagerEditor` (For Development)**: This is the full-featured manager used within the editor environment. Its primary role is to **import** source assets from common formats (e.g., `.gltf`, `.png`, `.jpg`).

2.  **`AssetManageRuntimer` (For Standalone Game Builds, currently not implemented)**: This will be a lightweight, high-performance manager designed for the final, shipped game where the editor is not present. It will not load base file formats like .jpg and just the assetpacks.

## Multithreaded Asynchronous Loading

A key feature of the asset management system is its ability to load assets asynchronously using a dedicated thread pool. This is crucial for performance, as it prevents the main thread from stalling while waiting for slow I/O operations and data processing to complete.

### The Loading Workflow

1.  **Request**: A system requests an asset using. If the asset is not yet loaded, and asynchronous loading is supported for that type of asset, an `AssetLoadRequest` is created, containing the asset's handle and a callback function to be executed upon completion.
2.  **Queue**: The request is pushed into a thread-safe queue.
3.  **Process**: Worker threads in a dedicated pool continuously monitor this queue. When a new request appears, a thread dequeues it and begins processing.
4.  **IO Loading**: The worker thread reads the asset file from disk and deserializes its data into an `Asset` object.
5.  **Store**: The newly loaded `Asset` is stored in a central cache of loaded assets.
6.  **Callback**: Finally, the worker thread invokes the callback function from the original request, notifying the requesting system that the asset is now ready for use.

### Performance Impact

This multithreaded approach dramatically improves loading times and application responsiveness. The main thread remains unblocked, allowing the game or editor to continue running smoothly (e.g., rendering a loading screen) while assets are loaded in the background.

The performance gains are substantial. For example, when loading the popular Sponza scene, this system reduced the total application startup and scene initialization time from over **30 seconds to less than 4 seconds**. This is a testament to the power of parallelizing I/O-bound and CPU-bound asset loading tasks, making the engine scalable for large and complex projects.

## Advanced Design for Flexibility and Type Safety

This section details how modern C++ features like templates, `std::variant`, and `std::optional` are used to create a highly flexible, extensible, and type-safe asset pipeline.

### Type-Safe Asset Handling via Templates

The system heavily relies on templates to ensure type safety and avoid error-prone `dynamic_cast` operations at runtime.

-   **Type-Safe Retrieval**: Functions like `AssetManager::GetAsset<T>(handle)` return a `std::shared_ptr<T>`, where `T` is the specific asset type (e.g., `Texture`, `Mesh`).
-   **Compile-Time Verification**: `static_assert` is used within these template functions to guarantee that `T` is a valid asset type. This catches type errors at compile time rather than as runtime exceptions.

### Per-Asset Configuration with `std::optional` and `std::variant`

A significant challenge in asset management is handling assets that require special import parameters without creating dozens of unique file types. This system solves the problem elegantly using `std::optional` and `std::variant`.

-   **The Problem**: A standard `.glsl` shader file might need custom preprocessor macros or additional include directories for compilation, unlike other `.glsl` files that use global settings. Creating a new file type (e.g., `.customshader`) for every variation is impractical.

-   **The Solution**: Each asset's `AssetMetadata` can hold an `std::optional<AssetImportConfig>`. This configuration is optional because most assets use default settings. When present, this config object provides specific instructions to the importer.

-   **Flexible Settings**: To support different kinds of settings for different asset types, the `AssetImportConfig` itself uses a `std::variant`. For instance, it might hold `std::variant<TextureImportSettings, ShaderImportSettings, ...>`. This allows storing type-safe, specific configuration data for any asset.

-   **Example Workflow (Shader Import)**:
    1.  The developer specifies in the editor that `MySpecialShader.glsl` requires a macro `USE_SPECIAL_FEATURE=1`.
    2.  The editor creates an `AssetImportConfig`, populates a `ShaderImportSettings` struct within its variant, and saves this config to the asset's metadata.
    3.  When the `ShaderImporter` processes `MySpecialShader.glsl`, it checks the metadata, finds the optional config, extracts the `ShaderImportSettings`, and adds the custom macro during shader compilation.

This approach provides powerful per-asset control, making the import pipeline extremely extensible while maintaining clean, generic importer logic.

## Notes

Currently only the texture loading is asynchronous because of the large performance gains. Adding support for more types will come as the engine develops.
In a later version of the engine I'am planning on implementing a job/task system. This asset manager can then be converted to use that system.
