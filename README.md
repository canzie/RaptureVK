# RaptureVK

RaptureVK is a high-performance 3D engine and editor built from the ground up in modern C++20, leveraging the power and flexibility of the Vulkan API. It is designed to explore and implement cutting-edge rendering techniques and high-performance engine architectures.

## ✨ Features

The engine is packed with advanced features, focusing on rendering quality, performance, and a modern, data-oriented architecture.

###  Rendering Subsystem

The heart of RaptureVK is its powerful Vulkan-based renderer, designed for high-fidelity graphics.

*   **Modern Vulkan Backend:** A clean, object-oriented C++20 wrapper around the Vulkan API, utilizing modern features like Dynamic Rendering, Descriptor Indexing (Bindless), and Ray Tracing extensions.
*   **Dynamic Diffuse Global Illumination (WIP):** A real-time global illumination solution using a probe-based system. It traces rays on the GPU to calculate irradiance and visibility, which are temporally blended to achieve stable, dynamic bounced lighting. Based on [this](https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf) paper.
*   **Cascaded Shadow Maps (CSM):** High-quality, dynamic shadows for directional lights covering large scenes. Renders meshes efficiently using Multi-Draw Indirect (MDI).
*   **Hybrid Rendering Pipeline:** Supports both **Forward** and **Deferred** rendering paths, allowing for flexibility in handling different material and lighting complexities.
*   **Physically-Based Rendering (PBR):** A robust material system designed for physically-based workflows, enabling realistic surfaces.

### Engine Architecture

Core systems are designed for performance, scalability, and ease of use.

*   **Asynchronous Asset Manager:** A multi-threaded, queue-based asset pipeline that loads resources like textures, models, and shaders in the background without stalling the main thread.
*   **High-Performance Buffer Management:** A sophisticated `BufferPoolManager` reduces the number of expensive Vulkan API calls by sub-allocating from large memory arenas. This improves performance by treating vertex and index data from many meshes as a single large buffer, significantly enhancing memory locality.
*   **Entity Component System (ECS):** The engine uses a data-oriented [ECS](https://github.com/sky) design for managing game objects and their properties, promoting clean code and high performance.
*   **Tracy Profiling:** Deeply integrated with the [Tracy Profiler](https://github.com/wolfpld/tracy) for detailed CPU and GPU performance analysis.

### Physics (WIP)

RaptureVK includes "Entropy," a custom-built, impulse-based rigid body physics engine. It was built based on Ian Millington's "Game Physics Engine Development" book.


*   **Rigid Body Dynamics:** Simulates the motion of objects using forces, torques, and constraints.
*   **Advanced Collision Detection:** Employs a multi-stage pipeline with a Bounding Volume Hierarchy (specifically, a dynamic BVH for moving objects and a static BVH for the world) to efficiently find collisions in complex scenes. A double dispatch is used for the narrow phase.
*   **Constraint Solver:** An iterative solver resolves collisions and other constraints to produce stable and physically-believable interactions.

### Rapture Editor

An integrated editor built with Dear ImGui provides tools for interacting with the engine.

*   **Scene Viewport:** A real-time view of the 3D scene with a camera that can be freely controlled.
*   **Entity Inspector:** View and modify the components and properties of objects in the scene.
*   **Content Browser:** Manage and import assets.

## 🚀 Future Development

RaptureVK is under active development. Here are some of the major features and systems that are planned:

*   **DDGI:** Add probe relocation and classification shaders and a glossy pipeline. Optimisations and improvements will come from these papers [[1](https://arxiv.org/pdf/2009.10796)] [[2](https://cescg.org/wp-content/uploads/2022/04/Rohacek-Improving-Probes-in-Dynamic-Diffuse-Global-Illumination.pdf)].
*   **Physics Engine:** Improve the interpenetration and velocity solvers by introcuding concepts like relaxing and realistic interpenetration solver. Add support for complex collider types using GJK. Support for easy configurable constraints.
*   **Node-Based Material Editor:** A visual tool for creating and customizing materials.
*   **Procedural Terrain System:** A system for generating vast, detailed landscapes using noise functions, LODs, and texture blending.
*   **Multithreaded Job System:** A general-purpose task system to parallelize engine workloads like physics, culling, and AI where systems can allocate tasks from a general thread pool.
*   **Advanced Rendering Features:**
    *   Screen-Space Ambient Occlusion (SSAO)
    *   A full Path Tracer for comparing other gi systems
    *   Volumetric Effects (Fog, Clouds) & Atmospheric Scattering
*   **Animation System:** Support state machines and blending.
*   **Full Serialization:** A robust system for saving and loading scenes and project assets.

## 🛠️ Building from Source

### Prerequisites

*   **C++20 Compiler:** Visual Studio 2022+, GCC 11+, or Clang 13+.
*   **CMake:** Version 3.16+.
*   **Vulkan SDK:** The latest version is recommended but currently 1.3 is required. Make sure `VULKAN_SDK` is set as an environment variable. 
*   **Linux/NVidia:** When using an NVidia GPU on linux make sure to use the latest drivers as some versions don't support the `VK_EXT_robustness2` extension.

### Build Steps

The project uses CMake with `FetchContent` to download and manage most dependencies automatically.

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/canzie/RaptureVK.git
    cd RaptureVK
    mkdir build
    cd build
    ```

2.  **Configure with CMake:**
    ```bash
    cmake ..
    ```

3.  **Build the project:**
    ```bash
    cmake --build . --config Release
    ```

4.  **Run the editor:**
    The executable `Rapture Editor` will be located in the `build/bin/Release` directory.