#pragma once

#include <string>
#include <array>

// Tracy profiling is enabled by default in debug builds and can be manually enabled in release builds
#ifndef RAPTURE_TRACY_PROFILING_ENABLED
    #if defined(RAPTURE_DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
        #define RAPTURE_TRACY_PROFILING_ENABLED 1
    #else
        #define RAPTURE_TRACY_PROFILING_ENABLED 0
    #endif
#endif

// Include Tracy when enabled
#if RAPTURE_TRACY_PROFILING_ENABLED
    #ifndef TRACY_ENABLE
    #define TRACY_ENABLE
    #endif
    #ifdef _WIN32
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
    #endif
    
    // Enable on-demand profiling to avoid buffering data when disconnected
    //#define TRACY_ON_DEMAND

    #include <vulkan/vulkan.h>

    // Now include Tracy headers
    #include <tracy/Tracy.hpp>
    #include <tracy/TracyVulkan.hpp>
#endif


// Define profiling macros based on Tracy availability
#if defined(RAPTURE_DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
    // CPU profiling
    #define RAPTURE_PROFILE_FUNCTION() ZoneScoped
    #define RAPTURE_PROFILE_SCOPE(name) ZoneScopedN(name)
    #define RAPTURE_PROFILE_FRAME() FrameMark
    #define RAPTURE_PROFILE_THREAD(name) tracy::SetThreadName(name)
    #define RAPTURE_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
    #define RAPTURE_PROFILE_FREE(ptr) TracyFree(ptr)
    
    // GPU profiling
    #define RAPTURE_PROFILE_GPU_SCOPE(name) \
        TracyVkZone(gpu_scope##__LINE__, name, true)
    #define RAPTURE_PROFILE_GPU_COLLECT() \
        TracyGpuCollect
    
    // Lock tracking
    #define RAPTURE_PROFILE_LOCKABLE(type, varname) TracyLockable(type, varname)
    #define RAPTURE_PROFILE_LOCKABLE_NAMED(type, varname, desc) TracyLockableN(type, varname, desc)
    #define RAPTURE_PROFILE_SHARED_LOCKABLE(type, varname) TracySharedLockable(type, varname)
    #define RAPTURE_PROFILE_SHARED_LOCKABLE_NAMED(type, varname, desc) TracySharedLockableN(type, varname, desc)
    #define RAPTURE_PROFILE_MUTEX(mtx) LockMark(mtx)
    
    // Plot values
    #define RAPTURE_PROFILE_PLOT(name, value) TracyPlot(name, value)
    
    // Message logging
    #define RAPTURE_PROFILE_MESSAGE(txt, size) TracyMessage(txt, size)
    #define RAPTURE_PROFILE_MESSAGE_COLOR(txt, size, color) TracyMessageC(txt, size, color)
    
#else
    // CPU profiling (empty macros)
    #define RAPTURE_PROFILE_FUNCTION()
    #define RAPTURE_PROFILE_SCOPE(name)
    #define RAPTURE_PROFILE_FRAME()
    #define RAPTURE_PROFILE_THREAD(name)
    #define RAPTURE_PROFILE_ALLOC(ptr, size)
    #define RAPTURE_PROFILE_FREE(ptr)
    
    // GPU profiling (empty macros)
    #define RAPTURE_PROFILE_GPU_SCOPE(name)
    #define RAPTURE_PROFILE_GPU_COLLECT()
    
    // Lock tracking (empty macros)
    #define RAPTURE_PROFILE_LOCKABLE(type, varname) type varname
    #define RAPTURE_PROFILE_LOCKABLE_NAMED(type, varname, desc) type varname
    #define RAPTURE_PROFILE_SHARED_LOCKABLE(type, varname) type varname
    #define RAPTURE_PROFILE_SHARED_LOCKABLE_NAMED(type, varname, desc) type varname
    #define RAPTURE_PROFILE_MUTEX(mtx)
    
    // Plot values (empty macros)
    #define RAPTURE_PROFILE_PLOT(name, value)
    
    // Message logging (empty macros)
    #define RAPTURE_PROFILE_MESSAGE(txt, size)
    #define RAPTURE_PROFILE_MESSAGE_COLOR(txt, size, color)
#endif

namespace Rapture {

// Interface class for Tracy integration, providing a simplified API
class TracyProfiler {
public:
    static void init();
    static void shutdown();
    
    // Called each frame to mark frame boundaries
    static void beginFrame();
    static void endFrame();
    
    // Initializes Tracy GPU context - should be called after OpenGL is fully initialized
    static void initGPUContext(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandBuffer cmdBuffer) {
    #if RAPTURE_TRACY_PROFILING_ENABLED
        if (!s_gpuInitialized) {
            s_gpuContext = TracyVkContext(physicalDevice, device, queue, cmdBuffer);
            s_gpuCommandBuffer = cmdBuffer;
            s_gpuInitialized = true;
        }
    #endif
    }
    
    // Collects GPU profiling data - should be called at the end of each frame
    static void collectGPUData() {
    #if RAPTURE_TRACY_PROFILING_ENABLED
        if (s_gpuInitialized) {
            TracyVkCollect(s_gpuContext, s_gpuCommandBuffer);
        }
    #endif
    }
    
    // Utility to check if Tracy is enabled
    static constexpr bool isEnabled() {
    #if RAPTURE_TRACY_PROFILING_ENABLED
        return true;
    #else
        return false;
    #endif
    }
    
    // Utility to check if we're in a debug build
    static bool isDebugBuild() {
    #if defined(RAPTURE_DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
        return true;
    #else
        return false;
    #endif
    }

private:
    static bool s_initialized;
    static bool s_gpuInitialized;
    #if RAPTURE_TRACY_PROFILING_ENABLED
    static TracyVkCtx s_gpuContext;
    static VkCommandBuffer s_gpuCommandBuffer;
    #endif
};

} // namespace Rapture 