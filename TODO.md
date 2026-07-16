uncouple the engines core
    - allow running headless (decoupling of graphics/window related stuff from the core), is a must for future headless servers

### ui/editor
    - create a nice terrain panel
    - set up a good way for settings, these need to then be serialised
        - color mapping/palette
        - hsv/hsl preference
        - couple sizings like gaps/borders/corners
        - an actual settings/preferences panel to open
    - drag/drop assets into the viewport
    - the ability to add/remove individual components from entities
        - includes depedencies like how a shadow component requires a light, this can be enforec in editor or in the engine, iam leaning towards engine

## Material Graph Node Editor 
    - when a new base mat is made and opened it cannot be viewd on a sphere, since we only render material instances, so for every material we will create a base instance, this one will be immutable
      it will make no changes and jsut take the defaults from the base material. there should be some kind of logic forcing this base instance to exist, so we cant delete it and its part of the base.
    - when editing a base materail in the editor it needs to be very simple to create a copy of a base.
    - adding or changing a material can mutate its gpu structure, we do not want this to cause out of bound reads, so it is important to make sure all places like gbuffers or probe trace shaders to be refreshed as soon as possible
      right now this only happens when we resize but iam not sure the shaders are recompiled, so what we need is a way to ask these places, like the gbuffer pass to recreate its pipeline using the new shader we compiled with the new material in it.
    - the properties panel now needs to be able to set a material isntance. it also eneds a button to open a material node editor workspace for the material, this also neds to happen in the content browser


### PHYSICS
    - integrate jolt
    - write a little wrapper for it
    - implement ray picking so we can select entities using it in the editor




- Make rendering things like bounds easier
  - current method is creating an instancedshapes component and providing the transformmatrix
  - this is akward as it cannot be used from inside the editor, only in code, we should be able to add it to certain things, like a mesh, terrain, etc, and depending on if we only need 1 or more to visualise debug use a simple mesh or instancing, like if the user selects aabb of a mesh, just a mesh, but if they select aabbs of the terrain we use instanced meshes, we can do this by seeing if the get aabb method returns 1 or multiple aabbs. , so it becomes a specific thing per mesh, per terrain comp etc. the thing to decide is how we enable/disable it while not storing the instanced data like the buffers in the same component.... 

- optimise the shadow passes
- make it run on windows???
- TODO later: BC7 (high quality RGBA) and BC6H (HDR) encoders
- fix stencil buffer
- shader/pipeline hot reloading
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
- audio
- ui
- game?


