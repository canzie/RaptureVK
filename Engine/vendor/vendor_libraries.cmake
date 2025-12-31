include(FetchContent)

# Configure FetchContent to cache downloaded content
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable updates for already downloaded content" FORCE)
set(FETCHCONTENT_QUIET FALSE CACHE BOOL "Enable verbose output for FetchContent" FORCE)

# Set cache directory to be inside the vendor directory
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/_deps" CACHE PATH "Base directory for downloaded content" FORCE)

# Prevent vendor libraries from being rebuilt due to policy changes
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Suppress warnings from vendor code
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "")
set(CMAKE_WARN_DEPRECATED OFF CACHE INTERNAL "")

# ==================== Vendor Libraries Configuration ====================

# Create a vendor interface target
add_library(vendor_libraries INTERFACE)

# Find system dependencies
find_package(Vulkan REQUIRED)

# ==================== Dependency Declarations ====================

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "Build GLFW with Wayland support" FORCE) 
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- GLM ---
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- ImGui ---
# Using v1.90.4 - stable release with docking support (March 2024)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4-docking
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- EnTT ---
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- spdlog ---
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- stb_image ---
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- Vulkan Memory Allocator (VMA) ---
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- SPIRV-Reflect ---
option(SPIRV_REFLECT_USE_SYSTEM_SPIRV_H "Use system spirv.h instead of the bundled one" OFF)
FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- yyjson ---
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.9.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- Tracy Profiler ---
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_DOWNLOAD ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- Shaderc Dependencies ---
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_SPVC OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_WGSL_OUTPUT OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_SHARED_CRT OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "" FORCE)

# Critical: Force glslang to use external SPIRV-Headers to avoid ODR violations
set(USE_EXTERNAL_SPIRV_HEADERS ON CACHE BOOL "" FORCE)

# Disable warnings and treat vendor code as system headers
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  spirv_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(spirv_headers)

FetchContent_Declare(
  spirv_tools
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
FetchContent_Declare(
  glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
FetchContent_Declare(
  shaderc
  GIT_REPOSITORY https://github.com/google/shaderc.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)

# --- tomlplusplus ---
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# --- concurrentqueue ---
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)


# ==================== Make Dependencies Available ====================

message(STATUS "=== Fetching Vendor Dependencies ===")
message(STATUS "This will only happen once. Subsequent builds will use cached versions.")

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(entt)
FetchContent_MakeAvailable(spdlog)
FetchContent_MakeAvailable(stb)
FetchContent_MakeAvailable(VulkanMemoryAllocator)
FetchContent_MakeAvailable(spirv_reflect)
FetchContent_MakeAvailable(yyjson)
FetchContent_MakeAvailable(tracy)

# Heavy shader compilation dependencies - built once and cached
message(STATUS "Fetching shader compilation dependencies (this may take a while on first build)...")
# Note: spirv_headers already made available earlier to avoid ODR violations
FetchContent_MakeAvailable(spirv_tools)
FetchContent_MakeAvailable(glslang)
FetchContent_MakeAvailable(shaderc)

FetchContent_MakeAvailable(tomlplusplus)
FetchContent_MakeAvailable(concurrentqueue)
message(STATUS "=== All vendor dependencies available ===")

# ==================== Suppress Warnings from Vendor Libraries ====================

# Function to mark all targets in a directory as SYSTEM
function(mark_as_system_includes target)
    if(NOT TARGET ${target})
        return()
    endif()
    
    # Check if it's an ALIAS target by trying to get ALIASED_TARGET property
    get_target_property(aliased_target ${target} ALIASED_TARGET)
    if(aliased_target)
        # It's an alias, skip it
        return()
    endif()
    
    get_target_property(target_type ${target} TYPE)
    
    # Skip INTERFACE_LIBRARY targets
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()
    
    # Suppress warnings from this target and disable LTO for vendor code
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-w>
        $<$<CXX_COMPILER_ID:MSVC>:/w>
    )
    
    # Suppress LTO warnings at link time (only for linkable targets)
    if(NOT target_type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU>:-Wno-odr -Wno-lto-type-mismatch -fno-lto>
        )
    endif()
    
    # Mark includes as SYSTEM to suppress warnings from headers
    get_target_property(include_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(include_dirs AND NOT include_dirs STREQUAL "include_dirs-NOTFOUND")
        set_target_properties(${target} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${include_dirs}"
        )
    endif()
endfunction()

# Suppress warnings from shader compilation libraries
mark_as_system_includes(SPIRV-Tools)
mark_as_system_includes(SPIRV-Tools-opt)
mark_as_system_includes(glslang)
mark_as_system_includes(SPIRV)
mark_as_system_includes(shaderc)
mark_as_system_includes(shaderc_combined)
mark_as_system_includes(glslang-default-resource-limits)

# Suppress warnings from other vendor libraries
mark_as_system_includes(glfw)
mark_as_system_includes(spdlog)
mark_as_system_includes(EnTT)

# ==================== Manual Target Configuration ====================

# --- Tracy Client Alias ---
# Tracy's CMake script should create this alias, but we create it here
# manually to be robust against different versions and configurations.
if(TARGET TracyClient AND NOT TARGET tracy::client)
    message(STATUS "Creating tracy::client alias for TracyClient target.")
    add_library(tracy::client ALIAS TracyClient)
endif()

# --- ImGui Target ---
add_library(imgui_static STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_static SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_static PUBLIC glfw Vulkan::Vulkan)

# Suppress ImGui warnings and disable LTO
target_compile_options(imgui_static PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/w>
)

# --- stb_image Target ---
set(STB_IMAGE_IMPL_FILE "${stb_BINARY_DIR}/stb_image_impl.cpp")
file(WRITE ${STB_IMAGE_IMPL_FILE} "#define STB_IMAGE_IMPLEMENTATION\n#include \"${stb_SOURCE_DIR}/stb_image.h\"\n")
add_library(stb_image STATIC ${STB_IMAGE_IMPL_FILE})
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})
target_compile_options(stb_image PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/w>
)

# --- VMA Target ---
add_library(vma STATIC ${vulkanmemoryallocator_SOURCE_DIR}/src/VmaUsage.cpp)
target_include_directories(vma SYSTEM PUBLIC ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_link_libraries(vma PUBLIC Vulkan::Vulkan)
target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=1)
set_target_properties(vma PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
target_compile_options(vma PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/w>
)

# --- SPIRV-Reflect Target ---
add_library(spirv_reflect_static STATIC ${spirv_reflect_SOURCE_DIR}/spirv_reflect.c)
target_include_directories(spirv_reflect_static SYSTEM PUBLIC ${spirv_reflect_SOURCE_DIR})
target_link_libraries(spirv_reflect_static PUBLIC Vulkan::Vulkan)
set_target_properties(spirv_reflect_static PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(SPIRV_REFLECT_USE_SYSTEM_SPIRV_H)
    target_compile_definitions(spirv_reflect_static PUBLIC SPIRV_REFLECT_USE_SYSTEM_SPIRV_H)
endif()
target_compile_options(spirv_reflect_static PRIVATE
    $<$<C_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
    $<$<C_COMPILER_ID:Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/w>
)

# --- yyjson Target ---
add_library(yyjson_static STATIC ${yyjson_SOURCE_DIR}/src/yyjson.c)
target_include_directories(yyjson_static SYSTEM PUBLIC ${yyjson_SOURCE_DIR}/src)
set_target_properties(yyjson_static PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_options(yyjson_static PRIVATE
    $<$<C_COMPILER_ID:GNU>:-w -Wno-odr -Wno-lto-type-mismatch -fno-lto>
    $<$<C_COMPILER_ID:Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/w>
)


# ==================== Link all libraries to vendor_libraries ====================
target_link_libraries(vendor_libraries INTERFACE
    glfw
    glm
    imgui_static
    EnTT
    spdlog
    stb_image
    vma
    spirv_reflect_static
    yyjson_static
    Vulkan::Vulkan
    tracy::client
    shaderc_combined
    tomlplusplus::tomlplusplus
    concurrentqueue
)

# Mark vendor_libraries includes as SYSTEM to suppress warnings
target_include_directories(vendor_libraries SYSTEM INTERFACE
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${entt_SOURCE_DIR}/single_include
    ${spdlog_SOURCE_DIR}/include
)

# Add platform-specific libraries for GLFW
if(WIN32)
    target_link_libraries(vendor_libraries INTERFACE gdi32 user32 shell32)
elseif(UNIX AND NOT APPLE)
    # Linux: Check for Wayland and X11 support
    
    # Check for Wayland
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND wayland-client wayland-cursor wayland-egl)
        if(WAYLAND_FOUND)
            message(STATUS "Wayland libraries found, linking Wayland support")
            target_link_libraries(vendor_libraries INTERFACE ${WAYLAND_LIBRARIES})
            target_include_directories(vendor_libraries INTERFACE ${WAYLAND_INCLUDE_DIRS})
        endif()
    endif()
    
    # Check for X11 (optional - link if available, may be needed by GLFW/ImGui even on Wayland)
    find_package(X11 QUIET)
    if(X11_FOUND)
        message(STATUS "X11 libraries found, linking X11 support")
        target_link_libraries(vendor_libraries INTERFACE ${X11_LIBRARIES})
    endif()
endif()

message(STATUS "=== Vendor libraries configuration complete ===")
message(STATUS "Vendor libraries will NOT rebuild unless you manually delete: ${FETCHCONTENT_BASE_DIR}")
