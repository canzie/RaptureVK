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


### Phase 2: Component Modularity (Main Goal)
Move rendering features into components for hot-swapping and graceful degradation.

- [x] **5. Create Environment entity pattern**
  - Scene stores entity ID (not ref) to a "Environment" entity via `Entity m_environmentEntity`
  - Scene has `createEnvironmentEntity()` method to create/get it
  - Components (SkyboxComponent, FogComponent, IndirectLightingComponent) added to this entity
  - Systems query for these components directly via `registry.view<T>()`
  - *Solution:* Scene refactored. mainCamera is `Entity m_mainCamera`, SceneSettings cleaned up to only actual settings. Environment entity pattern implemented.

- [x] **6. SkyboxComponent** - Extract skybox from hardcoded rendering
  - *Solution:* DeferredRenderer and DDGI now query `registry.view<SkyboxComponent>()` directly. Removed Scene::getSkyboxComponent(). Falls back gracefully if no skybox exists.

- [x] **7. FogComponent** - Make fog configurable per-scene
  - *Solution:* Created `Engine/src/Components/FogComponent.h` with fog settings (color, density, start/end, type, enabled). Ready for renderer integration.

- [x] **8. IndirectLightingComponent** - Could have general settings for gi, with possible children components for specifics like ddgi, static ambient, radiance cascades, lightmapping etc with specifics.
  - *Solution:* Created `Engine/src/Components/IndirectLightingComponent.h` with `std::variant<AmbientSettings, DDGISettings>` for technique selection. DDGI system has `updateFromIndirectLightingComponent()` to sync basic settings (grid dimensions, spacing, origin, rays per probe). Helper methods `isDDGI()`, `isAmbient()`, `isDisabled()`.

- [x] **9. Graceful degradation** - Systems work without optional components
  - No skybox? Renderer returns early or uses default
  - No IndirectLightingComponent? DDGI uses current/default settings
  - *Solution:* All systems check for component existence before use. Query patterns like `if (view.empty()) return;` ensure graceful fallback.

---

### Phase 2 Implementation Plan

**Approach:** One "Environment" entity per scene holds global rendering settings. Systems query for components on this entity (or any entity if we want flexibility).

**Step 1: Fix Scene's entity storage first**
- Change `SceneSettings` from `shared_ptr<Entity>` to storing `Entity` directly (it's already lightweight)
- Update `setMainCamera`, `setSkybox` etc. to store Entity by value
- This unblocks clean environment entity storage

**Step 2: Create Environment entity pattern**
```cpp
// In Scene or on scene creation:
Entity createEnvironmentEntity() {
    Entity env = createEntity("Environment");
    env.addComponent<SkyboxComponent>();      // optional, can be added later
    env.addComponent<FogComponent>();         // optional
    env.addComponent<IndirectLightingComponent>(); // optional
    return env;
}
```
- Scene stores this as `Entity m_environmentEntity`
- Or: don't store at all, just query `registry.view<SkyboxComponent>()` - more flexible

**Step 3: SkyboxComponent (already exists, just refactor usage)**
- Current: `Scene::getSkyboxComponent()` fetches from stored entity
- Change: Renderer queries `view<SkyboxComponent>()` directly
- Falls back to solid color if no skybox found

**Step 4: Create FogComponent**
```cpp
struct FogComponent {
    glm::vec3 color = glm::vec3(0.5f);
    float density = 0.01f;
    float start = 10.0f;    // for linear fog
    float end = 100.0f;
    FogType type = FogType::Exponential; // Linear, Exponential, ExponentialSquared
    bool enabled = true;
};
```
- Renderer queries for this, uses defaults if missing

**Step 5: Create IndirectLightingComponent**
```cpp
struct IndirectLightingComponent {
    // General GI settings
    float giIntensity = 1.0f;
    bool enabled = true;

    // Which technique to use (child component style via variant)
    std::variant<std::monostate, DDGISettings, AmbientSettings> technique;
};

struct DDGISettings {
    // get this from ddgi, we will store one inside of the ddgi object and one here
    // this way the ddgi system can check for any changes and has its own stable truth, and it can also pick which settings to take from outside.
};

struct AmbientSettings {
    glm::vec3 ambientColor = glm::vec3(0.03f);
};
```
- DDGI system checks for this component, reads settings from variant
- If no IndirectLightingComponent or disabled → use simple ambient

**Step 6: Update renderers for graceful degradation**
```cpp
// Pattern for each system:
void SkyboxPass::render(Scene& scene) {
    auto view = scene.getRegistry().view<SkyboxComponent>();
    if (view.empty()) {
        return;
    }
    // render skybox from first (or specific) entity
}
```

**Files to touch:**
```bash
# Find current skybox usage
grep -rn "getSkybox\|SkyboxComponent" Engine/ Editor/

# Find fog references (if any exist)
grep -rn "fog\|Fog" Engine/src/Renderer/

# Find DDGI config
grep -rn "DDGIConfig\|m_ddgi" Engine/src/Renderer/
```

**Order of implementation:**
1. Fix SceneSettings storage (shared_ptr → Entity)
2. Create FogComponent (new file: `Engine/src/Components/FogComponent.h`)
3. Create IndirectLightingComponent (new file or in Components.h for now)
4. Refactor SkyboxPass to query component directly
5. Add fog to lighting pass
6. Refactor DDGI to read from IndirectLightingComponent
7. Test graceful degradation (remove components, verify fallbacks)

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


