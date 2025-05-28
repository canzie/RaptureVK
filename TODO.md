for the IBO, VBO and UBOs (VkBuffer) store them in 1 VkBuffer instead of seperatly.



Look into making new child classes to the renderpass, like a deferred renderer pass
this would allow me to add mehtodss like cmdNextSubpass(Passes::GBUFFER) and cmdNextSubpass(Passes::LIGHTING) ...
i could also do this for shadow pass, csm pass, wireframe pass, etc.

then a material base would belong to a pipeline, like every shader in the GBuffer subpass  would need the same uniforms and attachements and output, ...




- lights
- imgui
- tracy
- deferred rendering
- shadow mapping
- compute shaders
- skybox
- static meshes
- materials in the asset manager
- ddgi
- stencil selection box
- mouse picking
- imgizmo
- physics
- animations
- giga serializaiton
- terrain
- post processing
- procedural stuff
- some limit testing
- audio
- ui
- game?

- Can you help me trasition my current engine from a standard renderpass/framebuffer pipeline to a dynamic rendering pipeline? the one introduced in vulkan 1.3. please make a plan on what needs to be updated and its dependencies otf the things you need to remove, then implement the new rendering pipeline



