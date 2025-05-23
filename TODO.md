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


Look into making new child classes to the renderpass, like a deferred renderer pass
this would allow me to add mehtodss like cmdNextSubpass(Passes::GBUFFER) and cmdNextSubpass(Passes::LIGHTING) ...
i could also do this for shadow pass, csm pass, wireframe pass, etc.

then a material base would belong to a pipeline, like every shader in the GBuffer subpass  would need the same uniforms and attachements and output, ...

