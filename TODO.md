
### Components

- WorldEnvironementComponent
    - Controls stuff like wind


During init we create a set of descriptors for different types
 - need custom types to differentiate between things like textures and texure arrays


struct DescriptorArrayType {
    TEXTURE,
    STORAGE_BUFFER,
    UNIFORM_BUFFER
 };

 In init we create an array for each type, then allocate smaller reserved slots for each system, this keeps memory local. good for cache.
 a system can ask for a reservation given a name, type and size, and possibly lifetime.


 - we will create a different descriptor array class, so use inheritence/abstractions
   because the buffers will need advanced logic by taking the underlying vkbuffer and just saving the allocation per array allocation.
   this way we can save descriptor slots, the only downside is that we need to send this data to the shader and deal with offsets.
   so the best way to start with this implementation is to either seperate only the suballocation classes or the orignal class.
   so for now we only need storage buffer and texture support, we will add the others when they are needed.


flow of the system should be like this:
 - an array for each type is created once.
 - a system (e.g. the shadowmapping) requests a suballocation and provides the manager a type and a size it needs (and a name for convience).
 - the system can then use that suballocation to  allocate/free resources.
 - it then needs to be possible to retrieve the descriptor set array to bind the set for usage. the bind indices can be static based on type.
   the caller will manage the set slot.



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
- completly redo the project setup structure to consitently build every lib the same way

--------------------------------

- ray picking

- skybox

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


