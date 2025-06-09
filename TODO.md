for the IBO, VBO and UBOs (VkBuffer) store them in 1 VkBuffer instead of seperatly.



Look into making new child classes to the renderpass, like a deferred renderer pass
this would allow me to add mehtodss like cmdNextSubpass(Passes::GBUFFER) and cmdNextSubpass(Passes::LIGHTING) ...
i could also do this for shadow pass, csm pass, wireframe pass, etc.

then a material base would belong to a pipeline, like every shader in the GBuffer subpass  would need the same uniforms and attachements and output, ...


### J*B SYSTEM
#### Requirements

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- a way to wait for a job to finish
- lightweight
- job queues can be static (dependency chain needs to be defined when the job queue is created)


TODO
- general descriptor manager
- fix the gizmo rotation math

--------------------------------

- ray picking

- skybox

- compute shaders
- ddgi

- static meshes

- materials in the asset manager

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

