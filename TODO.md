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
 - create a standalone queue submit for places like buffers and images, since they dont need to be pushing already saved command buffers since those might need a fence/semaphore

- bindless textures need to be organised
    - split the bindless arrays into categories like shadow maps, csm, etc.
    - need either (a) a global manager or (b) a manager per class that uses it
        -(a) ~~use enums for categorie, when adding a new type need to add it to both the enum and the class needs to add it~~
        -(b) static array, hosted by the class, might be some code duplication in all places -> create some template/abstract class for a bindless array. 
    


--------------------------------

- imgizmo
- frustum culling
- stencil selection box
- ray picking

- skybox

- shadow mapping

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

