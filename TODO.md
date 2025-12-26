
- [ ] Add entity locking support
  - Lock entities from editing and/or deletion (via PropertiesComponent flag?)
  - Useful for: editor UX, protecting essebntial entities like main camera
  - *Solution:*
- [ ] Component dependency hints (editor UX)
  - Shadow requires Light, CSM and regular shadow maps mutually exclusive
  - Validation at editor level, runtime still double-checks
  - Not critical - rare edge cases
- [ ] Reduce direct EnTT bypassing
  - Audit places where wrapper is bypassed for views/iteration
  - improve wrapper, as these bypasses are being done because of the inneficiency of the wrapper (maybe an actual 10x directly)



- fix stencil buffer
- shader hot reloading

- look at csm flickering again
- material editor/viewer
- jolt???
- look at removing the waitidles in some places like copyBuffer
- jobify the deferred renderer command recording
- allow for multiple heightmap
- allow for spline point (continentlenes, peaks and valleys, and erosion)
- materials for terrain
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

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- lightweight
- jobs need to be able to yield
- kind of like fibers
- seperate io thread to defer to jobs after
- static permanent job threads so the os does not redistribute them (like saying, oh yoink this thread, then os sees that other threads have been going for a while and take them for the one it took for itself, this cascades and caues multiple context switches for no reason)


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


