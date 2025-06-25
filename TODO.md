
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

# Descriptor System refactor

- global descriptor sets
- need a way to update descriptor set sizes dynamically (notify the pipeline of this change)
- create some static bindings
- shader reflection stays the same for the materials but changes for the other sets
  for those it will only provide the used set numbers and request the layouts from the descriptor manager
- for the material system, we create a seperate set for each material -> unique pipeline for each material
- we need a way to tell the system to add some binding to a set
    - provide either a set and binding number or a some enum it will then map the request to the predefined bindings
    - when setting some data to a binding, we request the binding, then assign whatever, if it is an array we find some free slot, set the data or 
      update and return the index this index will need to be used in the shader for access
    - a binding object will be a cpu object, updates to a specific index will go trough that object
      it needs to know which set it belongs to 
    - next is the syncing between cpu and glsl bindings ... for now we just do this manually? could probably import the same common file used in the shader...


class DescriptorBinding;
    - a reference/ptr to the set object it belongs to
    - a type (array, single descriptor) -> image and ssbo arrays should go trough the descriptorsetarray system only ubos are allowed to be arrays here
    - a method to add/update the data at its location (if it is an array also need the index)
    - the add method will return an index into the array if it is an array, otherwise something like 0
    - keep a vector of bools to track the current allocations, will be used when adding a new binding
    - a size
class DescriptorSet {
    - has a map of all the static bindings for a given set, this will be described at the start of the manager/application
    - containt the actual vkdescriptor set and layout
    - it will also contain a bind method that automatically binds to the given set, takes a commandbuffer and pipeline object (create one for compute 
      one for graphics)
    - need a config struct to properly define a set and its binding types

enum class with all of the binding types and their destined[] set (use some bit mask for this)
}



'''
    layout(set = 1, binding = 0) uniform materialUBOs[] {
        // unique to each material
        vec3 albedo;
        float roughness;
        float metallic;
        uint MetallicRougnesHandle;
        uint AlbedoHandle;
        uint NormalHandle;
        ...

        uint flags; // flags can be minimised by setting the handles to some reserved values
        // use bindless handles for the textures
    }

'''

CURRENT PHASE IN REFACTOR
 - new descriptor system is done-ish
 - need to update the
    7) update all shaders 
    8) see if the current camera component(or any other one) is correctly updating when the screen resizes
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


- terrain
- optimisations
    - parallel commandbuffer recording
    - lighting pass optimisations
    - look into reusing command buffer recordings
    - find a way to iterate over a view faster. -> even paralise that? -> we can, using secondary command buffers

--------------------------------

# features / stuff to add

- static meshes
- ssao
- (lod)
- mipmaps
- general descriptor manager

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


