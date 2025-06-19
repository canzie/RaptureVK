
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

- primitive meshes: for now we can add a primitive mesh, this will need to asign a material, mesh and transform. for the material we use some default material instance.
    for instancing: create an instance component, when this is added AND there is a meshcomponent also, it will create a vector of instanceIDs and transforms
    when drawing meshes, we will take entities that do not have this component, then do a seperate pass for the onesthat do, or use some if checks. the instance data should probably be on the gpu in some static buffer. then for the ui, in the mesh component part there will be a subsection with the instancing info showing the instance id and the instance specigic transform. user can select a different instance from this menu. for animation, we either dont allow those to be instanced or ignore behaviour as long as it does not crash. for materials ... maybe we just store a vector of components in the instance component.
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


