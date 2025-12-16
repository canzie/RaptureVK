## DX/UX Improvements (Priority Order)

### Phase 1: ECS Foundation (Do First)
These fixes enable the modularity work and improve reliability.

- [ ] **1. Entity reference overhaul** - Replace pointers/refs with wrapper-exposed IDs
  - Add an `EntityID` type to the wrapper (internally wraps entt::entity, but engine never sees that)
  - Store IDs directly, not Entity* or refs - pointers give false confidence
  - Add `Entity::getID()`, `Scene::isValid(EntityID)`, `Scene::getEntity(EntityID)`
  - Applies to: main camera, skybox, lighting entity refs in Scene
  - *Solution:*

- [ ] **2. Improve null/invalid entity handling**
  - Define a clear pattern for empty/default entities (failed returns, uninitialized)
  - Consider a static `Entity::Null()` or similar sentinel
  - *Solution:*

- [ ] **3. Add entity locking support**
  - Lock entities from editing and/or deletion (via PropertiesComponent flag?)
  - Useful for: editor UX, protecting essebntial entities like main camera
  - *Solution:*

- [x] **4. Rethink EntityNodeComponent hierarchy**
  - Current approach is awkward - investigate alternatives, a system from the ecs itself would be perfect, as simulating it via 'meta' components is not cool.
  - *Solution:* Created `HierarchyComponent` in `Engine/src/Components/HierarchyComponent.h`. Simple struct with `Entity parent` + `std::vector<Entity> children`. Free functions (`setParent`, `removeFromParent`, `destroyHierarchy`, `getRoot`) handle bidirectional sync. No shared_ptr, no separate EntityNode class. See refactor guide below.

---

### Hierarchy Refactor Guide (EntityNodeComponent → HierarchyComponent)

**New file:** `Engine/src/Components/HierarchyComponent.h`

**What changed:**
- Old: `EntityNodeComponent` → `shared_ptr<EntityNode>` → `shared_ptr<Entity>` + weak_ptr parent/children
- New: `HierarchyComponent` with `Entity parent` + `std::vector<Entity> children` directly

**Files to update (use grep to find all usages):**

```bash
# Find all EntityNodeComponent usages
grep -rn "EntityNodeComponent" Engine/ Editor/

# Find all EntityNode usages
grep -rn "EntityNode" Engine/ Editor/

# Find entity_node member access
grep -rn "entity_node" Engine/ Editor/
```

**Migration patterns:**

1. **Include change:**
   ```cpp
   // Old
   #include "Components/Systems/EntityNode.h"
   // New
   #include "Components/HierarchyComponent.h"
   ```

2. **Adding hierarchy (e.g., glTFLoader):**
   ```cpp
   // Old
   childEntity.addComponent<EntityNodeComponent>(
       std::make_shared<Entity>(childEntity),
       nodeEntity.getComponent<EntityNodeComponent>().entity_node);
   nodeEntity.getComponent<EntityNodeComponent>().entity_node->addChild(
       childEntity.getComponent<EntityNodeComponent>().entity_node);

   // New
   Rapture::setParent(childEntity, nodeEntity);
   ```

3. **Getting parent:**
   ```cpp
   // Old
   auto& nodeComp = entity.getComponent<EntityNodeComponent>();
   auto parentNode = nodeComp.entity_node->getParent();
   auto parentEntity = parentNode->getEntity();

   // New
   auto& hier = entity.getComponent<HierarchyComponent>();
   Entity parentEntity = hier.parent;
   ```

4. **Getting children:**
   ```cpp
   // Old
   auto children = nodeComp.entity_node->getChildren();
   for (auto& childNode : children) {
       auto childEntity = childNode->getEntity();
   }

   // New
   auto& hier = entity.getComponent<HierarchyComponent>();
   for (Entity child : hier.children) {
       // use child directly
   }
   ```

5. **Checking if has parent:**
   ```cpp
   // Old
   if (nodeComp.entity_node && nodeComp.entity_node->getParent())

   // New
   if (hier.hasParent())
   ```

**Files that need updating:**
- `Engine/src/Loaders/glTF2.0/glTFLoader.cpp` - main user, creates hierarchies
- `Editor/src/imguiPanels/BrowserPanel.cpp` - reads hierarchy for UI tree
- `Engine/src/Components/Components.h` - remove EntityNodeComponent, add include

**After migration:**
- Delete `Engine/src/Components/Systems/EntityNode.h`
- Delete `Engine/src/Components/Systems/EntityNode.cpp` (if exists)
- Remove EntityNodeComponent from Components.h

---

### Phase 2: Component Modularity (Main Goal)
Move rendering features into components for hot-swapping and graceful degradation.

- [ ] **5. Create Lighting entity with components**
  - Scene stores entity ID (not ref) to a "Lighting" entity
  - Contains: SkyboxComponent, FogComponent, IndirectLightingComponent, etc.
  - Systems query for these components - work without them if missing, and if not possible to use the lighting entity, query any available, or lock these component into only being allowed in the lighting component
  - **Child components**: Editor shows nested components (e.g. IndirectLightingComponent > DDGIComponent), internally can be std::variant or similar - editor UX vs internal representation can differ

- [ ] **6. SkyboxComponent** - Extract skybox from hardcoded rendering
  - *Solution:*

- [ ] **7. FogComponent** - Make fog configurable per-scene
  - *Solution:*

- [ ] **8. IndirectLightingComponent** - Could have general settings for gi, with possible children components for specifics like ddgi, static ambient, radiance cascades, lightmapping etc with specifics.
  - *Solution:*

- [ ] **9. Graceful degradation** - Systems work without optional components
  - No camera? Don't render until one exists (editor cam vs player cam)
  - No skybox? Render solid color or skip
  - No DDGI? Fall back to ambient

---

### Phase 3: Quality of Life (Lower Priority)

- [ ] **10. Component dependency hints** (editor UX)
  - Shadow requires Light, CSM and regular shadow maps mutually exclusive
  - Validation at editor level, runtime still double-checks
  - Not critical - rare edge cases

- [ ] **11. Reduce direct EnTT bypassing**
  - Audit places where wrapper is bypassed for views/iteration
  - improve wrapper, as these bypasses are being done because of the inneficiency of the wrapper (maybe an actual 10x directly)

---

### Future (Not Now)

- **Serialization with schemas** - Define component structure once, store values compactly (not JSON-style key repetition)
- **Transform component refactor** - Currently stores no data itself, consider helper methods/functions

---
---
- look at csm flickering again
- material editor/viewer
- fix stencil buffer
- jolt???
- RC in 3d????, should be attemptable with opus ~ 50 bucks








### Components

- WorldEnvironementComponent
    - Controls stuff like wind




### DDGI - shit is hard

- stabilize the ray rotation
- add probe relocation and classification to the ddgi system
- add support for different probe volumes
- test system in one of the test scenes
- generate HDR cubemap for the skybox

- perhaps giga optimisation
    - get a new shader pass for identifying which probes should be active
    - we can do this by stopping certain probes from updating if ...
    https://arxiv.org/pdf/2009.10796
    https://cescg.org/wp-content/uploads/2022/04/Rohacek-Improving-Probes-in-Dynamic-Diffuse-Global-Illumination.pdf




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


### Terrain Generation

- allow for easy noise generation, should only need inputs, outputs and algo -> provide an image
- make basic terrain
- blend multiple noise layers
- use tesselation shaders
- generate normals, use textures, deal with collision??? 
- add lod and spatial partitioning
- look into clipmaps


### J*B SYSTEM
#### Requirements

- easy to add new jobs
- easy to set dependencies between jobs
- a way to cancel jobs and its children
- a way to wait for a job to finish
- lightweight
- job queues can be static (dependency chain needs to be defined when the job queue is created)


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


