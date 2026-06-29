uncouple the engines core
    - allow running headless (decoupling of graphics/window related stuff from the core), is a must for future headless servers

### ui/editor
    - create a proper material editor workspace
    - create a nice terrain panel
    - set up a good way for settings, these need to then be serialised
        - color mapping/palette
        - hsv/hsl preference
        - couple sizings like gaps/borders/corners
        - an actual settings/preferences panel to open
    - drag/drop assets into the viewport
        - maybe just "rightclick->import into scene" for now
    - mesh property thing needs its asset ref, same for material thing.
    

    - !!! get the editor to a point where we can actually delete entityies, easily add some, even if just cubes/spheres for now. 
    - fix the stencil buffer for rendering the outline of selected objects


- Make rendering things like bounds easier
  - current method is creating an instancedshapes component and providing the transformmatrix
  - this is akward as it cannot be used from inside the editor, only in code, we should be able to add it to certain things, like a mesh, terrain, etc, and depending on if we only need 1 or more to visualise debug use a simple mesh or instancing, like if the user selects aabb of a mesh, just a mesh, but if they select aabbs of the terrain we use instanced meshes, we can do this by seeing if the get aabb method returns 1 or multiple aabbs. , so it becomes a specific thing per mesh, per terrain comp etc. the thing to decide is how we enable/disable it while not storing the instanced data like the buffers in the same component.... 

- optimise the shadow passes
- make it run on windows???
- texture compression DONE (BC1/BC3/BC4/BC5 via compute)
  - TODO later: BC7 (high quality RGBA) and BC6H (HDR) encoders
- fix stencil buffer
- shader hot reloading
- jolt???
- parallise/jobify shader compilation (note, current stack size is too small for this, maybe spawn another process and use gslang exec???)
- add model to the asset manager
  - ditinction between static and dynamic meshes here
  - the asset importer in the editor will be able to set these options and they can be either metadata or ...
    - editor settings could be static/dynamic, prefab options?
    - animation options etc (once they exist)
    - checkox for importing material from gltf and auto making the materials and applying them
- pre generated normals?
- virtual texturing??? like decima i guess
- phyics -> raypicking -> terrain editor and mesh placer

### PHYSICS
    - raypicking, can be part of the physics system with something like this: physics.raycast(ray, ...)
    - fixing the imguizmo thing    
    - the larger problem with open world means we need a seperate tlas for the tlasses per chunk, this can be one on the cpu.
    - fix issue where the dynamic tree does not remove entities when they are removed
    - redo all of the maths, including addition of balancing when something falls, it should rotate to go flat
    - add option to view the meshes collider seperatly instead of only the aabb, and move this collider -> add it to the entity browser directly
    - move physics object and updates to the scenes onupdate, instead of the testlayer



### MATERIAL OVERHAUL 
- add material interpreter
- add material editor



### Render passes
    - defined as what is now a gbuffer pass, shadow pass, skybox pass etc etc, needs to become more generic so we can easily add them
    - this means:
        - a more uniform interface
        - some kind of exec order
    - this would clean up the unfloded stuff at the bottom of the file where we record all passes
    - makes it easier to add things like a post process step or even a user defined pass. if we ever decide to allow users to insert into the rendering pipeline a nicer interface will be provided, not the raw vulkan one used by something like the gbuffer pass, but thats for later. for now we just allow providing an exec order and a nicer way to record commands


--------------------------------

# features / stuff to add

- ssao
- ss reflections
- animations
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


