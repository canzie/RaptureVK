
### Components

- WorldEnvironementComponent
    - Controls stuff like wind




### DDGI - shit is hard

- use random rays
- check what to do with backface hits/ general results from the ray trace

- add probe relocation to the ddgi system
- add support for different probe volumes
- fix the weird artifacts, probably related to normals or something
- test system in one of the test scenes
- generate HDR cubemap for the skybox

- perhaps giga optimisation
    - get a new shader pass for identifying which probes should be active
    - we can do this by stopping certain probes from updating if ...
    https://arxiv.org/pdf/2009.10796
    https://cescg.org/wp-content/uploads/2022/04/Rohacek-Improving-Probes-in-Dynamic-Diffuse-Global-Illumination.pdf




### PHYSICS
    - raypicking, can be part of the physics system with something like this: physics.raycast(ray, ...)
    - fixing the imguizmo thing    
    - the larger problem with open world means we need a seperate tlas for the tlasses per chunk, this can be one on the cpu.
    - fix issue with adding/removing/resizing entities in the static tree
    - fix issue where the dynamic tree does not remove entities when they are removed
    - redo all of the maths, including addition of balancing when something falls, it should rotate to go flat
    - add option to view the meshes collider seperatly instead of only the aabb, and move this collider -> add it to the entity browser directly
    - move physics object and updates to the scenes onupdate, instead of the testlayer



### MATERIAL OVERHAUL 
- add user generated materials
- tldr: use a template gbuffer shader (currently just fragment), create some node with the pbr outputs as the final node
        let the user do intermediate operations like generating or importing textures (everything is bindless anyway), also provide them with some default inputs
        if a connection in the final node is empty, we use defaults meaning everything empty is the default material, could add an option to disable this
        or they just override the default values
    -> need a seperate descriptor set for each different material -> sort by material for the mdi batch
- add material editor


### Terrain Generation

- allow for easy noise generation, should only need inputs, outputs and algo -> provide an image
- make basic terrain
- blend multiple noise layers
- use tesselation shaders
- generate normals, use textures, deal with collision??? 
- add lod and spatial partitioning
- look into clipmaps


### J*B SYSTEM
#### Requirements

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- a way to wait for a job to finish
- lightweight
- job queues can be static (dependency chain needs to be defined when the job queue is created)


### ASSET MANAGER
- materials in the asset manager -> Material Graph editor
- meshes in the asset manager

### ISSUES
- fix the gizmo translation math
- fix CSM on triple buffering
- optimisations
    - lighting pass optimisations
- [ 9 implement a weakptr caching system in the bindings -> resize support (after the base works) ]
- fix the issue where not rendering the sponza scene -> not tlas -> crash, should be able to render empty scene or a full scene no matter what (except vertex only meshes)

--------------------------------

# features / stuff to add

- static meshes
- ssao
- path tracer ...
- Photometry (use camera settings to calculate the correct exposure)
- animations
- giga serializaiton
- post processing
- some limit testing
- volumetric fog/clouds
- atmospheric scattering
- audio
- ui
- game?


