for the IBO, VBO and UBOs (VkBuffer) store them in 1 VkBuffer instead of seperatly.

# Vulkan Abstractions

- Framebuffers ✅
- Swapchain ✅
- Renderpass ✅// Dont know which parameters need tweeking so default for no
- Pipelines ❌
 - needs shaders     -> class
 - needs vertex info -> class
 - topology
 - viewpoer/scissor  -> framebuffer?
 - rasterizer
 - multisampling
 - color blend
 - pipeline layout

- Command Buffers ❌
- Command Pools ❌
- Sync Objects ❌

- Buffers 🟨
