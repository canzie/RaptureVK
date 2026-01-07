

- jobify mesh loading (probably cant store data on the stack so circumvent this by letting the main or io thread load it and provide a ptr when its loaded)
- Make rendering things like bounds easier
  - current method is creating an instancedshapes component and providing the transformmatrix
  - this is akward as it cannot be used from inside the editor, only in code, we should be able to add it to certain things, like a mesh, terrain, etc, and depending on if we only need 1 or more to visualise debug use a simple mesh or instancing, like if the user selects aabb of a mesh, just a mesh, but if they select aabbs of the terrain we use instanced meshes, we can do this by seeing if the get aabb method returns 1 or multiple aabbs. , so it becomes a specific thing per mesh, per terrain comp etc. the thing to decide is how we enable/disable it while not storing the instanced data like the buffers in the same component.... 
- fix the scuffed shit where the chunk data is first writting manually and then via the compute shader, its shit i hate it

- fix stencil buffer
- shader hot reloading
- jolt???
- look at removing the waitidles in some places like copyBuffer
- parallise/jobify shader compilation
- add material to asset manager
- add model to the asset manager
  - ditinction between static and dynamic meshes here
  - the asset importer in the editor will be able to set these options and they can be either metadata or ...
    - editor settings could be static/dynamic, prefab options?
    - animation options etc (once they exist)
    - checkox for importing material from gltf and auto making the materials and applying them
- pre generated normals?
- virtual texturing??? like decima i guess
- phyics -> raypicking -> custom gizmo and terrain editor and mesh placer

### PHYSICS
    - raypicking, can be part of the physics system with something like this: physics.raycast(ray, ...)
    - fixing the imguizmo thing    
    - the larger problem with open world means we need a seperate tlas for the tlasses per chunk, this can be one on the cpu.
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


### J*B SYSTEM
#### Requirements
- see design document

### ASSET MANAGER
- materials in the asset manager -> Material Graph editor
- meshes in the asset manager

### ISSUES
- fix the gizmo translation math
- fix CSM on triple buffering
- optimisations
    - lighting pass optimisations

--------------------------------

# features / stuff to add

- static meshes
- ssao
- ss reflections
- animations
- job system
- scripting
- jolt
- Photometry (use camera settings to calculate the correct exposure)
- giga serializaiton
- post processing
- some limit testing
- volumetric fog/clouds
- atmospheric scattering
- audio
- ui
- game?


