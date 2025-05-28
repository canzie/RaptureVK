# Vendor Libraries Configuration

# Create a vendor interface target
add_library(vendor_libraries INTERFACE)

# Find system dependencies
find_package(Vulkan REQUIRED)

# Print the current directory for debugging
message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")

# ==================== GLFW ====================
# Only look in Engine/vendor for libraries
set(GLFW_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/GLFW")
message(STATUS "Looking for GLFW at ${GLFW_DIR}")

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# ---> ADDED: Enable Wayland support if building from source <---
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "Build GLFW with Wayland support" FORCE) 

# GLFW will be used with Vulkan, ensure Vulkan headers are available for its compilation if built from source.
# No specific GLFW_USE_VULKAN flag is standard, GLFW detects Vulkan at runtime.

if(EXISTS "${GLFW_DIR}/include/GLFW/glfw3.h")
    message(STATUS "GLFW headers found")
    
    # Check if using pre-built libraries or building from source
    if(EXISTS "${GLFW_DIR}/lib/glfw3.lib")
        message(STATUS "Using pre-built GLFW static library")
        add_library(glfw STATIC IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW_DIR}/lib/glfw3.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
        )
    elseif(EXISTS "${GLFW_DIR}/lib/glfw3dll.lib")
        message(STATUS "Using pre-built GLFW dynamic library")
        add_library(glfw SHARED IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_IMPLIB "${GLFW_DIR}/lib/glfw3dll.lib"
            IMPORTED_LOCATION "${GLFW_DIR}/bin/glfw3.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include"
        )
    elseif(EXISTS "${GLFW_DIR}/CMakeLists.txt")
        message(STATUS "Building GLFW from source")
        add_subdirectory(${GLFW_DIR} glfw EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "GLFW library not found in ${GLFW_DIR}/lib")
    endif()
else()
    message(FATAL_ERROR "GLFW headers not found in ${GLFW_DIR}/include")
endif()

# ==================== GLM ====================
# Only look in Engine/vendor for libraries
set(GLM_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/glm")
message(STATUS "Looking for GLM at ${GLM_DIR}")

# Try to find GLM in different possible structures
if(EXISTS "${GLM_DIR}/glm/glm.hpp")
    message(STATUS "GLM found")
    # GLM directly in vendor/glm
elseif(EXISTS "${GLM_DIR}/glm-master/glm/glm.hpp")
    set(GLM_DIR "${GLM_DIR}/glm-master")
    message(STATUS "GLM found in glm-master")
elseif(EXISTS "${GLM_DIR}/glm.hpp")
    set(GLM_DIR "${GLM_DIR}")
    message(STATUS "GLM found in glm root")
else()
    # Search recursively for glm.hpp
    file(GLOB_RECURSE GLM_HEADER "${GLM_DIR}/**/glm/glm.hpp")
    
    if(GLM_HEADER)
        # Get the directory containing glm/glm.hpp
        get_filename_component(GLM_PATH ${GLM_HEADER} DIRECTORY)
        get_filename_component(GLM_DIR ${GLM_PATH} DIRECTORY)
        message(STATUS "Found GLM using recursive search at ${GLM_DIR}")
    else()
        message(FATAL_ERROR "GLM not found. Please place GLM in Engine/vendor/glm directory.")
    endif()
endif()

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${GLM_DIR})

# ==================== ImGui ====================
# Only look in Engine/vendor for libraries
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/imgui")
message(STATUS "Looking for ImGui at ${IMGUI_DIR}")

if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(STATUS "ImGui found")
    file(GLOB IMGUI_SOURCES ${IMGUI_DIR}/*.cpp)
    add_library(imgui STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui PUBLIC 
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends  # Add backends directory to include paths
    )
    
    # ImGui Vulkan backend
    if(EXISTS "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp" AND EXISTS "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp")
        message(STATUS "ImGui Vulkan/GLFW backends found")
        target_sources(imgui PRIVATE 
            ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        )
        # Link imgui to GLFW (for imgui_impl_glfw) and Vulkan::Vulkan (for imgui_impl_vulkan)
        # This provides the necessary include directories and link dependencies for ImGui's backend compilation.
        target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)
    else()
        message(WARNING "ImGui Vulkan/GLFW backends not found. Placeholder files might be used. Please ensure correct backend files are in ${IMGUI_DIR}/backends/")
        # Create placeholders if they don't exist. User should replace these.
        if(NOT EXISTS "${IMGUI_DIR}/backends")
            file(MAKE_DIRECTORY ${IMGUI_DIR}/backends)
        endif()
        if(NOT EXISTS "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")
            file(WRITE ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp "// Generated empty file - Please add actual ImGui Vulkan backend\n#include <vulkan/vulkan.h> // Add a dummy include to check if Vulkan headers are found\nvoid ImGui_ImplVulkan_Init() {}")
        endif()
        if(NOT EXISTS "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp")
            file(WRITE ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp "// Generated empty file - Please add actual ImGui GLFW backend\nstruct GLFWwindow; \nvoid ImGui_ImplGlfw_InitForVulkan(GLFWwindow* window, bool install_callbacks) {}")
        endif()
        target_sources(imgui PRIVATE 
            ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        )
        # Even with placeholders, attempt to link for consistency and to catch header issues early.
        target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)
    endif()
else()
    message(FATAL_ERROR "ImGui not found in ${IMGUI_DIR}. Please clone from https://github.com/ocornut/imgui.git (docking branch) and ensure backends are available.")
endif()

# ==================== EnTT ====================
# Only look in Engine/vendor for libraries
set(ENTT_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/entt")
message(STATUS "Looking for EnTT at ${ENTT_DIR}")

if(EXISTS "${ENTT_DIR}/include/entt/entt.hpp")
    message(STATUS "EnTT found")
    add_library(entt INTERFACE)
    target_include_directories(entt INTERFACE ${ENTT_DIR}/include)
else()
    # Might be nested in a subdirectory
    file(GLOB_RECURSE ENTT_HEADER "${ENTT_DIR}/**/include/entt/entt.hpp")
    
    if(ENTT_HEADER)
        get_filename_component(ENTT_PATH ${ENTT_HEADER} DIRECTORY)
        get_filename_component(ENTT_INCLUDE_DIR ${ENTT_PATH} DIRECTORY)
        get_filename_component(ENTT_DIR ${ENTT_INCLUDE_DIR} DIRECTORY)
        
        message(STATUS "EnTT found using recursive search at ${ENTT_DIR}")
        add_library(entt INTERFACE)
        target_include_directories(entt INTERFACE ${ENTT_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "EnTT not found in ${ENTT_DIR}")
    endif()
endif()

# ==================== spdlog ====================
# Only look in Engine/vendor for libraries
set(SPDLOG_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/spdlog")
message(STATUS "Looking for spdlog at ${SPDLOG_DIR}")

# Configure spdlog build options
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Build spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build spdlog tests" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Generate spdlog install target" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build spdlog as a shared library" FORCE) # Build as static

# Check if spdlog has its own CMakeLists.txt (source version)
# The get_vendor_libs scripts should place the extracted spdlog contents (which include CMakeLists.txt)
# directly into Engine/vendor/spdlog
if(EXISTS "${SPDLOG_DIR}/CMakeLists.txt")
    message(STATUS "Building spdlog from source using add_subdirectory(${SPDLOG_DIR})")
    add_subdirectory(${SPDLOG_DIR} spdlog EXCLUDE_FROM_ALL) # Creates 'spdlog' target from Engine/vendor/spdlog/CMakeLists.txt
else()
    # This case should ideally not be hit if get_vendor_libs script works correctly.
    message(FATAL_ERROR "spdlog/CMakeLists.txt not found in ${SPDLOG_DIR}. Please ensure the library is correctly downloaded and placed by the get_vendor_libs script, then re-run CMake.")
endif()

# Make sure we set the correct definition for compiled mode for the spdlog target.
# These will propagate to targets linking spdlog because they are PUBLIC.
target_compile_definitions(spdlog PUBLIC 
    SPDLOG_COMPILED_LIB
    FMT_HEADER_ONLY=0
)

# ==================== stb_image ====================
# Only look in Engine/vendor for libraries
set(STB_IMAGE_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/stb_image")
message(STATUS "Looking for stb_image at ${STB_IMAGE_DIR}")

if(EXISTS "${STB_IMAGE_DIR}/stb_image.h")
    message(STATUS "stb_image found")
    # Change from INTERFACE to STATIC and include the source file
    if(EXISTS "${STB_IMAGE_DIR}/stb_image.cpp")
        add_library(stb_image STATIC "${STB_IMAGE_DIR}/stb_image.cpp")
    else()
        # stb_image.h typically includes the implementation if STB_IMAGE_IMPLEMENTATION is defined.
        # Create a .cpp file that defines STB_IMAGE_IMPLEMENTATION and includes stb_image.h
        set(STB_IMAGE_IMPL_FILE "${CMAKE_CURRENT_BINARY_DIR}/stb_image_impl.cpp")
        file(WRITE ${STB_IMAGE_IMPL_FILE} "#define STB_IMAGE_IMPLEMENTATION\n#include \"${STB_IMAGE_DIR}/stb_image.h\"\n")
        add_library(stb_image STATIC ${STB_IMAGE_IMPL_FILE})
    endif()
    target_include_directories(stb_image PUBLIC ${STB_IMAGE_DIR})
else()
    # Try to find recursively
    file(GLOB_RECURSE STB_IMAGE_HEADER "${CMAKE_SOURCE_DIR}/Engine/vendor/**/stb_image.h")
    
    if(STB_IMAGE_HEADER)
        get_filename_component(STB_IMAGE_DIR_REC ${STB_IMAGE_HEADER} DIRECTORY)
        message(STATUS "stb_image found using recursive search at ${STB_IMAGE_DIR_REC}")
        
        # Check for stb_image.cpp in the found directory or create implementation file
        set(STB_IMAGE_SOURCE_REC "${STB_IMAGE_DIR_REC}/stb_image.cpp")
        if(EXISTS "${STB_IMAGE_SOURCE_REC}")
             add_library(stb_image STATIC ${STB_IMAGE_SOURCE_REC})
        else()
            set(STB_IMAGE_IMPL_FILE_REC "${CMAKE_CURRENT_BINARY_DIR}/stb_image_impl_rec.cpp")
            file(WRITE ${STB_IMAGE_IMPL_FILE_REC} "#define STB_IMAGE_IMPLEMENTATION\n#include \"${STB_IMAGE_HEADER}\"\n")
            add_library(stb_image STATIC ${STB_IMAGE_IMPL_FILE_REC})
        endif()
        target_include_directories(stb_image PUBLIC ${STB_IMAGE_DIR_REC})
    else()
        message(FATAL_ERROR "stb_image.h not found in ${STB_IMAGE_DIR} or subdirectories.")
    endif()
endif()

# ==================== Vulkan Memory Allocator (VMA) ====================
# Only look in Engine/vendor for libraries
set(VMA_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/VulkanMemoryAllocator")
message(STATUS "Looking for Vulkan Memory Allocator at ${VMA_DIR}")

if(EXISTS "${VMA_DIR}/CMakeLists.txt")
    message(STATUS "Building Vulkan Memory Allocator from source using add_subdirectory(${VMA_DIR})")
    
    # Configure VMA build options
    set(VMA_STATIC_VULKAN_FUNCTIONS ON CACHE BOOL "Link statically with Vulkan" FORCE)
    set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF CACHE BOOL "Dynamic Vulkan functions" FORCE)
    set(VMA_BUILD_SAMPLE OFF CACHE BOOL "Build VMA sample" FORCE)
    set(VMA_BUILD_SAMPLE_SHADERS OFF CACHE BOOL "Build VMA sample shaders" FORCE)
    
    # Create our own VMA target since the CMakeLists.txt in VMA doesn't provide one by default
    add_library(vma STATIC "${VMA_DIR}/src/VmaUsage.cpp")
    target_include_directories(vma PUBLIC "${VMA_DIR}/include")
    target_link_libraries(vma PUBLIC Vulkan::Vulkan)
    
    # Define VMA_STATIC_VULKAN_FUNCTIONS for the entire project
    target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=1)
    
    # Ensure VMA builds with the same C++ standard as the main project
    set_target_properties(vma PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
    )
else()
    message(FATAL_ERROR "VulkanMemoryAllocator/CMakeLists.txt not found in ${VMA_DIR}. Please ensure the library is correctly downloaded and placed, then re-run CMake.")
endif()

# ==================== SPIRV-Reflect ====================
# Only look in Engine/vendor for libraries
set(SPIRV_REFLECT_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/SPIRV-Reflect")
message(STATUS "Looking for SPIRV-Reflect at ${SPIRV_REFLECT_DIR}")

if(EXISTS "${SPIRV_REFLECT_DIR}/spirv_reflect.h")
    message(STATUS "SPIRV-Reflect found")
    
    # Create a static library from the source files
    add_library(spirv_reflect STATIC 
        "${SPIRV_REFLECT_DIR}/spirv_reflect.c"
    )
    
    # Add include directories
    target_include_directories(spirv_reflect PUBLIC ${SPIRV_REFLECT_DIR})
    
    # Link with Vulkan
    target_link_libraries(spirv_reflect PUBLIC Vulkan::Vulkan)
    
    # Use C99 standard for spirv_reflect.c
    set_target_properties(spirv_reflect PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
    )
    
    # Add option for systems using their own SPIRV headers
    option(SPIRV_REFLECT_USE_SYSTEM_SPIRV_H "Use system spirv.h instead of the bundled one" OFF)
    if(SPIRV_REFLECT_USE_SYSTEM_SPIRV_H)
        target_compile_definitions(spirv_reflect PUBLIC SPIRV_REFLECT_USE_SYSTEM_SPIRV_H)
    endif()
else()
    message(FATAL_ERROR "SPIRV-Reflect not found in ${SPIRV_REFLECT_DIR}. Please ensure the library is correctly downloaded and placed, then re-run CMake.")
endif()

# ==================== yyjson ====================
# Only look in Engine/vendor for libraries
set(YYJSON_DIR "${CMAKE_SOURCE_DIR}/Engine/vendor/yyjson")
message(STATUS "Looking for yyjson at ${YYJSON_DIR}")

if(EXISTS "${YYJSON_DIR}/yyjson.h" AND EXISTS "${YYJSON_DIR}/yyjson.c")
    message(STATUS "yyjson found")
    
    # Create a static library from the source files
    add_library(yyjson STATIC 
        "${YYJSON_DIR}/yyjson.c"
    )
    
    # Add include directories
    target_include_directories(yyjson PUBLIC ${YYJSON_DIR})
    
    # Use C99 standard for yyjson.c (it's an ANSI C library)
    set_target_properties(yyjson PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
    )
else()
    message(FATAL_ERROR "yyjson.h or yyjson.c not found in ${YYJSON_DIR}. Please ensure the library is correctly downloaded and placed by the get_vendor_libs script, then re-run CMake.")
endif()

# ==================== Link all libraries to vendor_libraries ====================
target_link_libraries(vendor_libraries INTERFACE
    glfw
    glm
    imgui
    entt
    spdlog
    stb_image
    vma       # Link VMA
    spirv_reflect # Link SPIRV-Reflect
    yyjson         # Link yyjson
    Vulkan::Vulkan
)

# Add Windows-specific libraries for GLFW
if(WIN32)
    target_link_libraries(vendor_libraries INTERFACE gdi32 user32 shell32)
endif()

# Include directories for all vendor libraries
# Vulkan include directories are handled by linking Vulkan::Vulkan
target_include_directories(vendor_libraries INTERFACE
    ${GLFW_DIR}/include
    ${GLM_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends  # Add backends directory
    ${ENTT_DIR}/include  # Assuming entt includes are in ENTT_DIR/include
    ${SPDLOG_DIR}/include # Assuming spdlog includes are in SPDLOG_DIR/include
    ${STB_IMAGE_DIR}     # Assuming stb_image.h is directly in STB_IMAGE_DIR
    ${VMA_DIR}/include    # Add VMA include directory
    ${SPIRV_REFLECT_DIR}  # Add SPIRV-Reflect include directory
    ${YYJSON_DIR}         # Add yyjson include directory
)
# Ensure correct include paths for EnTT and spdlog if they were found recursively
if(ENTT_INCLUDE_DIR)
    target_include_directories(vendor_libraries INTERFACE ${ENTT_INCLUDE_DIR})
endif()
if(SPDLOG_INCLUDE_DIR_MANUAL) # This variable was not set, check spdlog section logic if it was intended
    # If spdlog was built manually, its include dir was set on the spdlog target itself.
    # It might be already propagated via spdlog INTERFACE properties, or might need explicit addition.
    # For safety, let's re-evaluate. The spdlog target itself gets the include dir.
    # If spdlog is INTERFACE, it propagates. If STATIC, need to check its INTERFACE_INCLUDE_DIRECTORIES.
    # The current spdlog setup adds include dirs to the spdlog target itself as PUBLIC.
    # Linking spdlog should make its public include directories available.
endif()

# Ensure include directory for stb_image if found recursively
if(STB_IMAGE_DIR_REC)
    target_include_directories(vendor_libraries INTERFACE ${STB_IMAGE_DIR_REC})
endif()
if(VMA_DIR) # Though VMA_DIR is set directly above, this is for consistency with other blocks
    target_include_directories(vendor_libraries INTERFACE ${VMA_DIR}/include)
endif()
if(YYJSON_DIR) # For consistency
    target_include_directories(vendor_libraries INTERFACE ${YYJSON_DIR})
endif()

# Cleanup variables that might conflict if this script is re-included
unset(GLM_HEADER)
unset(GLM_PATH)
unset(ENTT_HEADER)
unset(ENTT_PATH)
unset(ENTT_INCLUDE_DIR)
unset(STB_IMAGE_HEADER)
unset(STB_IMAGE_SOURCE)
unset(STB_IMAGE_DIR_REC)
unset(STB_IMAGE_IMPL_FILE)
unset(STB_IMAGE_IMPL_FILE_REC)
unset(VMA_DIR) # Cleanup VMA_DIR
unset(YYJSON_DIR) # Cleanup YYJSON_DIR
