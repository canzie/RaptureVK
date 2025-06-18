
### Components

- WorldEnvironementComponent
    - Controls stuff like wind




### DDGI - shit is hard
- add probe relocation to the ddgi system
- add support for different probe volumes
- fix the weird artifacts, probably related to normals or something
- test system in one of the test scenes
- generate HDR cubemap for the skybox

- !!! update/rebuild the tlas



# current focus

- physics
    - raypicking, can be part of the physics system with something like this: physics.raycast(ray, ...)
    - nice way to add some primitive meshes
    - fixing the imguizmo thing
    - ability to EASILY draw/add either a bbox debug to a mesh or switch to wireframe, this is needed to visualise the colliders

- terrain
- optimisations


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

# features / stuff to add

- ray picking
- static meshes
- ssao
- (lod)
- mipmaps
- materials in the asset manager
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


