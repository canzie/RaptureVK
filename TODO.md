
### Components

- WorldEnvironementComponent
    - Controls stuff like wind




### DDGI - shit is hard
- add probe relocation to the ddgi system
- add support for different probe volumes
- fix the weird artifacts, probably related to normals or something
- test system in one of the test scenes
- generate HDR cubemap for the skybox
- use random rays

- perhaps giga optimisation
    - get a new shader pass for identifying which probes should be active
    - we can do this by stopping certain probes from updating if ...
    https://arxiv.org/pdf/2009.10796
    https://cescg.org/wp-content/uploads/2022/04/Rohacek-Improving-Probes-in-Dynamic-Diffuse-Global-Illumination.pdf



# current focus

- physics
    - raypicking, can be part of the physics system with something like this: physics.raycast(ray, ...)
    - nice way to add some primitive meshes
    - fixing the imguizmo thing
    - ability to EASILY draw/add either a bbox debug to a mesh or switch to wireframe, this is needed to visualise the colliders

    - we need a static tree and a dynamic tree
    - each frame we check the dynamic object against both of these trees, the dynamic one will be one that is updated quickly while the static one will need fast traversal/accuracy
    - the larger problem with open world means we need a seperate tlas for the tlasses per chunk, this can be one on the cpu.


    - start with broad phase collision
    - then we can go to narrow phase collision, by checking the actual collision -> then test by changing color when 2 objects intersect
    - after this we can go back and implement a solver

    - create these datastructures to be hotswappable


broad phase
    - check simple aabb collision against the bvh

narrow phase
    - check collisions with the actual colliders -> output is either colliding entities or a contact manifold


BVH, DBVH, BVH_SAH are implemented, now we should do some limit testing before we move forward. Could load like x cubes/spheres and disable rendering


NEED a way to visualise probes, aabbs, etc. using instancing...
    - we have the primitives
    - seperate pass vs combined pass -> ???


  [ 9) implement a weakptr caching system in the bindings -> resize support (after the base works) ]


RENDERER/MATERIAL OVERHAUL 



### J*B SYSTEM
#### Requirements

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- a way to wait for a job to finish
- lightweight
- job queues can be static (dependency chain needs to be defined when the job queue is created)


TODO
- ray picking
- materials in the asset manager -> Material Graph editor
- fix the gizmo translation math
- fix CSM on triple buffering


- terrain
- optimisations
    - lighting pass optimisations

--------------------------------

# features / stuff to add

- static meshes
- ssao
- (lod)


- emmisive materials
- Photometry (use camera settings to calculate the correct exposure)

- animations
- giga serializaiton

- post processing
- procedural stuff
- some limit testing
- audio
- ui
- game?


