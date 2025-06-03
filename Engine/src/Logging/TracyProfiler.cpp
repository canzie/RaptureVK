#include "TracyProfiler.h"
#include "Logging/Log.h"

namespace Rapture {

bool TracyProfiler::s_initialized = false;
bool TracyProfiler::s_gpuInitialized = false;

#if RAPTURE_TRACY_PROFILING_ENABLED
TracyVkCtx TracyProfiler::s_gpuContext = nullptr;
VkCommandBuffer TracyProfiler::s_gpuCommandBuffer = VK_NULL_HANDLE;
#endif

void TracyProfiler::init() {
    if (s_initialized) {
        return;
    }
    
    #if RAPTURE_TRACY_PROFILING_ENABLED
        // Set the main thread name
        tracy::SetThreadName("Main Thread");
        
        // Log initialization
        RP_CORE_INFO("Tracy Profiler initialized");
        
        // GPU context will be initialized later when Vulkan is ready
    #else
        RP_CORE_WARN("Tracy Profiler is disabled. Build with RAPTURE_TRACY_PROFILING_ENABLED=1 to enable.");
    #endif
    
    s_initialized = true;
}

void TracyProfiler::shutdown() {
    if (!s_initialized) {
        return;
    }
    
    #if RAPTURE_TRACY_PROFILING_ENABLED
        // Clean up GPU context if initialized
        if (s_gpuInitialized && s_gpuContext) {
            TracyVkDestroy(s_gpuContext);
            s_gpuContext = nullptr;
            s_gpuCommandBuffer = VK_NULL_HANDLE;
            s_gpuInitialized = false;
        }
        
        // No additional explicit shutdown needed for Tracy
        RP_CORE_INFO("Tracy Profiler shutdown");
    #endif
    
    s_initialized = false;
}

void TracyProfiler::beginFrame() {
    #if RAPTURE_TRACY_PROFILING_ENABLED
        // Mark the beginning of a new frame
        FrameMark;
    #endif
}

void TracyProfiler::endFrame() {
    // Tracy automatically handles frame boundaries with FrameMark
    // No additional work needed here
}

} // namespace Rapture 