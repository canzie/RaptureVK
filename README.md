# RaptureVK

RaptureVK is a 3D engine and editor written in C++20 using the Vulkan API.

## Features

* Vulkan backend using Dynamic Rendering, Descriptor Indexing (Bindless), and Ray Tracing extensions
* Deferred rendering path
* Cascaded Shadow Maps
* Dynamic Diffuse Global Illumination (DDGI)
* Physically-based material system
* Asynchronous, multi-threaded asset loading
* Fiber-based job system, inspired by Naughty Dog's GDC talk on their engine's job system
* Editor built with Amethyst, a custom UI library

## Building from Source

### Prerequisites

* C++20 compiler: Visual Studio 2022+, GCC 11+, or Clang 13+
* CMake 3.16+
* Vulkan SDK 1.3+, with the `VULKAN_SDK` environment variable set
* Linux/NVidia: latest drivers are required for the `VK_EXT_robustness2` extension
* Mainly tested using native Wayland but "should" work on X11 and Windows, but don't quote me.

### Build Steps

```bash
git clone https://github.com/canzie/RaptureVK.git
cd RaptureVK
git submodule update --init --recursive

mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The `Rapture Editor` executable will be located in `build/bin/Release`.
