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
    - ability to hot reload an ams file, and update the actual stuff, pretty sure marking the window dirty and updating the style maps and invalidating cache is all it takes.
    - make the gizmo support discrete steps, also the gizmo direction maybe be cooked.
    - check all the clears done, and which ones are redundant.

### Materials 
    - when a new base mat is made and opened it cannot be viewd on a sphere, since we only render material instances, so for every material we will create a base instance, this one will be immutable
      it will make no changes and jsut take the defaults from the base material. there should be some kind of logic forcing this base instance to exist, so we cant delete it and its part of the base.
      -> tldr, take all the default values from a base material and create a read-only/lock instance that is tied to the lifetime of the base-material, engine owned
    - adding or changing a material can mutate its gpu structure, we do not want this to cause out of bound reads, so it is important to make sure all places like gbuffers or probe trace shaders to be refreshed as soon as possible
      right now this only happens when we resize but iam not sure the shaders are recompiled, so what we need is a way to ask these places, like the gbuffer pass to recreate its pipeline using the new shader we compiled with the new material in it.
    - the properties panel now needs to be able to set a material isntance. it also eneds a button to open a material node editor workspace for the material, this also neds to happen in the content browser
    
    - expand the material system, probably need to update the gbuffer too. so stuff like dielectrics

### Terrain
    - Add a path to use custom heightmaps first, for the time being it will take priority over fully procedural worksflows
    - fix the random black triangles
    - get a basic editor panel going for adjusting certain parameters about how it is rendered
    - go over the material, check if terrain materials are a strict superset of regular materials (if yes then we can use reg mats on terrain)
    - create a nicer terrain material, maybe using noise or some actual textures->texture import flow


### Engine Architecture
    - Add a concept of ownership, can be something like ENGINE, EDITOR, DEVELEOPER, USER. these could then be used for modify access to things like internal assets
      

- Make rendering things like bounds easier
- optimise the shadow passes
- make it run on windows???
- TODO later: BC7 (high quality RGBA) and BC6H (HDR) encoders
- shader/pipeline hot reloading
- parallise/jobify shader compilation (note, current stack size is too small for this, maybe spawn another process and use gslang exec???)
  - the asset importer in the editor will be able to set these options and they can be either metadata or ...
    - editor settings could be static/dynamic, prefab options?
    - animation options etc (once they exist)
    - checkox for importing material from gltf and auto making the materials and applying them
- pre generated normals?
- virtual texturing??? like decima i guess

--------------------------------

# features / stuff to add

- animations
- scripting
- Photometry (use camera settings to calculate the correct exposure)
- post processing
- some limit testing
- volumetric fog/clouds
- audio
- ui(in game)
- game?


