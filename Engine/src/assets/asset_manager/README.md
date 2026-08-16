# Asset Management System

This document outlines the architecture of the engine's asset management system, a robust, multithreaded pipeline designed for high-performance loading and organized handling of game assets.

## System Overview

The asset manager is responsible for the entire lifecycle of assets, from their initial import to their use at runtime. It is built around a few core concepts:

-   **Asset**: An in-memory representation of game data, such as a mesh, texture, or material. All assets are stored as an `AssetVariant` and are identified by a unique `AssetHandle` (a UUID).
-   **Asset Metadata**: Information about an asset, including its handle, type, file path, and its `useCount`.
-   **Asset Registry**: A central catalog that maps every `AssetHandle` to its corresponding `AssetMetadata`. This registry acts as a manifest of all available assets for the project.
-   **AssetRef**: A reference-counted wrapper around an asset. It increments and decrements the `useCount` in the `AssetMetadata`, allowing the system to track how many systems are currently using an asset.

### Editor vs. Standalone Game Asset Management

The system features two distinct asset manager implementations tailored for different stages of the production pipeline:

1.  **`AssetManagerEditor` (For Development)**: This is the full-featured manager used within the editor environment. Its primary role is to **import** source assets from common formats (e.g., `.gltf`, `.png`, `.jpg`).

2.  **`AssetManagerRuntime` (For Standalone Game Builds, currently not implemented)**: This will be a lightweight, high-performance manager designed for the final, shipped game where the editor is not present. It will not load base file formats like .jpg and just the assetpacks.

## Fiber-Based Asynchronous Loading

A key feature of the asset management system is its ability to load assets asynchronously using the engine's fiber-based `JobSystem`. This is crucial for performance, as it prevents the main thread from stalling while waiting for slow I/O operations and data processing to complete.

### The Loading Workflow (Asynchronous)

1.  **Request**: When an asset is requested (e.g., `Texture::loadAsync`), an asynchronous load job is submitted to the `JobSystem`.
2.  **IO Thread**: The system uses a dedicated **IO Thread** to handle blocking file reads. The job yields its fiber using `jctx.waitFor` until the IO operation completes, allowing the worker thread to process other jobs in the meantime.
3.  **Processing**: Once data is read, the fiber resumes on a worker thread to process the data (e.g., decoding a PNG using `stbi_load_from_memory`).
4.  **GPU Upload (Transfer Queue)**: For GPU assets like textures, the system utilizes the Vulkan **Transfer Queue** for asynchronous uploads.
5.  **GPU Wait**: The fiber yields again while waiting for the GPU to signal completion via a timeline semaphore, utilizing `jobs().submitGpuWait`.
6.  **Finalization**: Once the upload is complete, the fiber resumes, marks the asset as `READY`, and triggers any completion callbacks.

### Performance and Scalability

This fiber-based approach dramatically improves loading times and application responsiveness. The main thread remains unblocked, allowing the game or editor to continue running smoothly while assets are loaded in the background. By using fibers, the loading logic can be written in a straightforward, sequential style even though it involves multiple asynchronous wait points.

The performance gains are substantial. For example, when loading the popular Sponza scene, this system reduced the total application startup and scene initialization time from over **30 seconds to less than 3 seconds**.

## Advanced Design for Flexibility and Type Safety

This section details how modern C++ features like templates, `std::variant`, and `std::optional` are used to create a highly flexible, extensible, and type-safe asset pipeline.

### Type-Safe Asset Handling via Templates

The system heavily relies on templates to ensure type safety and avoid error-prone `dynamic_cast` operations at runtime.

-   **Type-Safe Retrieval**: Functions like `Asset::getUnderlyingAsset<T>()` return a pointer to the specific asset type (e.g., `Texture`, `Mesh`), verified against the `AssetVariant`.
-   **Compile-Time Verification**: `static_assert` and template specialization are used to guarantee that `T` is a valid asset type.

### Per-Asset Configuration with `std::optional` and `std::variant`

A significant challenge in asset management is handling assets that require special import parameters without creating dozens of unique file types. This system solves the problem elegantly using `std::optional` and `std::variant`.

-   **The Problem**: A standard `.glsl` shader file might need custom preprocessor macros or additional include directories for compilation, unlike other `.glsl` files that use global settings.
-   **The Solution**: Each asset's `AssetMetadata` can hold an `std::optional<AssetImportConfig>`. This configuration is optional because most assets use default settings. When present, this config object provides specific instructions to the importer.
-   **Flexible Settings**: To support different kinds of settings for different asset types, the `AssetImportConfig` itself uses a `std::variant`. For instance, it might hold `std::variant<TextureImportSettings, ShaderImportSettings, ...>`.

## Notes

- Currently, texture loading is fully integrated with the fiber-based asynchronous pipeline. Other asset types (Shaders, Scenes, Materials) are progressively being moved to this system.
- The use of `AssetRef` instead of `std::shared_ptr` was a deliberate design choice to allow the `AssetManager` to retain full ownership and control over the asset's lifecycle while providing a safe, reference-counted interface to the rest of the engine.
