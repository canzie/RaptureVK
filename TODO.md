uncouple the engines core - allow running headless (decoupling of
graphics/window related stuff from the core), is a must for future headless
servers

### ui/editor

    - create a nice terrain panel
    - set up a good way for settings, these need to then be serialised
        - color mapping/palette
        - hsv/hsl preference
        - couple sizings like gaps/borders/corners
        - an actual settings/preferences panel to open
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


- make it run on windows???
- TODO later: BC7 (high quality RGBA) and BC6H (HDR) encoders
- shader/pipeline hot reloading
- parallise/jobify shader compilation (note, current stack size is too small for
  this, maybe spawn another process and use gslang exec???)
- virtual texturing??? like decima i guess

---
- make a new panel for the scene object tree where components can be authored, also check if we should allow this sort of inline?
- parseanimations from gltf
- animation events
- editor side of this, including a timeline for the animation and a panel to view skeletons? or skeletal meshes maybe? or maybe just a skeleton panel with a mesh preview?
- get lua in for something basic, then get a basic text editor in the engine, nothing fancy, then check for highlighting.
- have a talk about extensions/plugins
- plan out undo/redo 
- auto save
- editor settings
- project settings
- a way to check if something is dirty, like if a scene objects wasnt saved yet, then show a little circle on the workspace tab
- snapping, copy pasting scene objects, shortcuts for all kind of stuff.
- selection border
- snapping when sizing/using gizmo, snapping when moving items, like corner snapping, or like when free moving a cube make it so it gets placed on floor behind/below it, so it drags over the floor instead of trough?
- forward+ renderer ontop for blended materials
- add support for masked objects in gbuffer pass (like folliage)
- audio.

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
