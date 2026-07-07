# Per-node Graph Values

**Parent: [[Material Graph Compiler]] (fixes its §10). Related: [[Material System Overhaul]], [[GraphInstanceData]], [[MaterialInstance]].**

The design to make an authored value on *any* pin — not just a `CONSTANT` node's single output — a first-class thing that reaches the GPU as a runtime-editable pool constant. This closes the "per-node values are underpowered" limitation written up in [[Material Graph Compiler]] §10.

---

## 0. The limitation, verified against source

The §10 write-up is accurate. Confirmed against the current tree:

- **`GraphNode` carries exactly one baked value.** `MaterialGraph.h:21` — `PinValue constantValue{};` with the comment "CONSTANT nodes: the slot value". There is no other value storage on the node (the only other data field is `AssetPtr<Texture> texture` at `MaterialGraph.h:22`).
- **Only `CONSTANT` nodes consume it.** `s_assignResources` writes `defaults.constants[slot] = node->constantValue.v4;` **only** in the `ResourceKind::CONSTANT` branch (`MaterialGraphCompiler.cpp:164-170`). `ResourceKind` is assigned per node type in `NodeRegistry.cpp` (e.g. `CONSTANT_VEC3` at `NodeRegistry.cpp:81-84`); every math/reader/sink node is `ResourceKind::NONE` (the struct default, `MaterialGraphTypes.h:165`).
- **One `{const}` placeholder per node.** `s_emitNodeExpr` substitutes a single `{const}` from `mapping.constantSlots[node.id]` (`MaterialGraphCompiler.cpp:212-214`), and falls back to `expr = "{const}"` for an empty template on a constant node (`:204`). `mapping.constantSlots` is keyed by **node id**, one slot per node (`MaterialGraphCompiler.h:24`).
- **Every other unconnected input uses the *shared* def default.** `s_resolveInput` returns `s_literal(pin.defaultValue, pin.type)` when a pin has no incoming connection (`MaterialGraphCompiler.cpp:189-190`). `pin.defaultValue` lives on the shared `NodeDefinition.PinDef` (`MaterialGraphTypes.h:150`), not on the node — e.g. every `MULTIPLY_VEC3` shares `{"a", VEC3, 1.0f}` (`NodeRegistry.cpp:130-133`). It cannot be authored per placed node.
- **`SURFACE_OUTPUT`'s unconnected channels use hardcoded compiler fallbacks.** The sink lambda in `s_emitSurfaceBody` returns a literal string when a channel is unconnected: `"vec3(1.0)"`, `"normalize(si.worldNormal)"`, `"0.5"`, `"0.0"`, `"1.0"` (`MaterialGraphCompiler.cpp:264-269`). `SURFACE_OUTPUT` is `ResourceKind::NONE` with only inputs and no template (`NodeRegistry.cpp:383-388`), so it can bake nothing.

The editor already models what the IR cannot store: `PinView.value` is a `std::unique_ptr<Rapture::PinValue>` — "authored value: an input default or a source output" (`NodeEditorPanel.h:47`) — and `NodeControl.value` is a `std::unique_ptr<Rapture::PinValue>` per control (`NodeEditorPanel.h:56`). These have nowhere to land in `MaterialGraph`.

**Net:** the editor can author a value on any pin, but only a `CONSTANT` node's value survives into `compile()`. This design gives the rest a home.

---

## 1. The one idea

An **authored value on an unconnected input pin, or on a control, is a pool constant** — exactly like a `CONSTANT` node's value is today. It gets a `constants[]` slot, is pre-filled into `CompileResult.defaults`, is recorded in the mapping so the editor can rewrite it, and is read back in the generated GLSL with a type swizzle. Dragging a slider becomes a data write to that slot — **no recompile** — the same property the single constant node already has (`Material.cpp:98-102` writes the whole 320 B slice; nothing recompiles).

Two things stay true so existing graphs are untouched:

1. An input the user **never authored** keeps emitting the shared `PinDef.defaultValue` as a baked literal (today's `s_literal` path). Only *touched* values get a slot. This keeps the 16-constant budget from exploding and makes the change byte-identical for the current generated file until someone authors something.
2. The `{const}` template mechanism is a special case of the general per-value scheme, not a separate thing.

---

## 2. IR changes (`MaterialGraph.h`)

Replace the single `constantValue` with two sparse stores. Sparse matters: only authored entries exist, so untouched inputs cost nothing and stay baked literals.

```cpp
struct GraphNode {
    uint32_t id = 0;
    GraphNodeType type = GraphNodeType::NONE;

    // Authored value for an unconnected INPUT pin, keyed by input-pin index.
    // Absent key == not authored -> compiler bakes the shared PinDef.defaultValue.
    std::unordered_map<uint32_t, PinValue> inputValues;

    // CONTROL values (a value the node exposes with no input pin, e.g. a CONSTANT node's
    // output value). Dense: controlValues[c] is control c. Empty for nodes with no controls.
    std::vector<PinValue> controlValues;

    AssetPtr<Texture> texture = {}; // unchanged: TEXTURE nodes' bound texture
};
```

- A `CONSTANT_*` node's value moves from `constantValue` to `controlValues[0]`. This is the *only* migration to existing authored graphs (see §6).
- `PinValue` is unchanged (`MaterialGraphTypes.h:132-145`); it already stores whichever scalar/vector type via the union.
- The editor's `PinView.value` (`NodeEditorPanel.h:47`) serializes into `inputValues[slotIndex]`; `NodeControl.value` (`NodeEditorPanel.h:56`) serializes into `controlValues[controlIndex]`. `PinView.slotIndex` (`NodeEditorPanel.h:43`) is already the input-pin index the map is keyed on.

Deciding *which* values are slot-backed (live) vs baked into GLSL is deferred to the constant/variable split already flagged in [[Material Graph Compiler]] §9 — Phase 1 treats every authored value as slot-backed (live-editable), which is the safe superset.

---

## 3. Compiler changes (`MaterialGraphCompiler.cpp`)

### 3.1 Placeholder scheme

Keep the existing `{pinName}` placeholders; **do not** invent per-input placeholders. Input pins already route through `s_resolveInput` (`MaterialGraphCompiler.cpp:206-208`), so making that function slot-aware upgrades every math/sink node with zero template edits. Generalize only the value-source placeholder:

| Placeholder | Meaning | Change |
|-------------|---------|--------|
| `{pinName}` | expression feeding input pin `pinName` | unchanged token; `s_resolveInput` now returns a **pool ref** for an authored unconnected input, else the baked literal as today |
| `{tex}` | this node's texture slot | unchanged (single texture per node) |
| `{const}` | control 0's slot (alias for `{const0}`) | kept for back-compat with the 5 `CONSTANT_*` templates (`NodeRegistry.cpp:71-88`) |
| `{const0}`, `{const1}`, … | control `c`'s slot | new; for multi-control nodes |

A pool constant is a `vec4` slot (`GraphInstanceData.constants[]`), so reading it back at a narrower pin type needs a swizzle: `FLOAT → .x`, `VEC2 → .xy`, `VEC3 → .xyz`, `VEC4 → (whole)`, `INT → int(...x)`. This is the swizzle the existing constant templates already spell out by hand (`{const}.xyz`, `int({const}.x)`, `NodeRegistry.cpp:75-84`). Factor it into a helper:

```cpp
static std::string s_poolSwizzle(uint32_t slot, PinType type); // s_poolRef("constants", slot) + swizzle
```

For input placeholders the *template* has no swizzle (a `MULTIPLY_VEC3` writes `{a} * {b}`), so `s_resolveInput` must apply the swizzle itself when it returns a pool ref. It already applies a swizzle via `s_coerce` for connected inputs (`:194`), so this is symmetric.

### 3.2 `s_resolveInput` becomes slot-aware

```cpp
static std::string s_resolveInput(..., const GraphNode &node, const GraphSlotMapping &mapping,
                                  uint32_t nodeId, const PinDef &pin, uint32_t pinIndex)
{
    const GraphConnection *c = s_findInputConnection(graph, nodeId, pinIndex);
    if (c != nullptr) { /* unchanged: upstream var, coerced (:192-194) */ }

    // NEW: unconnected but authored -> its pool slot, read at the pin type
    if (auto slot = mapping.inputSlots.find(s_slotKey(nodeId, pinIndex)); slot != mapping.inputSlots.end())
        return s_poolSwizzle(slot->second, pin.type);

    // unchanged: untouched -> shared def default baked as a literal (:190)
    return s_literal(pin.defaultValue, pin.type);
}
```

Order is load-bearing: connection wins over authored value wins over baked default. That matches the editor (a wired pin ignores its stored value).

### 3.3 `s_assignResources` assigns multiple per-node slots

Walk the topo order as today (`MaterialGraphCompiler.cpp:159-179`), but a single node may now reserve several `constants[]` slots. For each node in order:

1. **Control values** — for each entry in `node.controlValues`, reserve one constant slot, pre-fill `defaults.constants[slot] = value.v4`, record `mapping.controlSlots[s_slotKey(nodeId, c)] = slot`. (A `CONSTANT_*` node has exactly one control, reproducing today's behavior; its `{const}`/`{const0}` resolves through this.)
2. **Authored input values** — for each `(pinIndex, value)` in `node.inputValues`, reserve one constant slot, pre-fill it, record `mapping.inputSlots[s_slotKey(nodeId, pinIndex)] = slot`.
3. **Texture** — unchanged (`:171-178`).

`s_slotKey(nodeId, index)` reuses the packing already present as `s_emitKey` (`MaterialGraphCompiler.cpp:21-24`). Assignment stays in topo order so slot numbers are deterministic.

`GraphSlotMapping` (`MaterialGraphCompiler.h:23-26`) gains keyed-by-composite maps:

```cpp
struct GraphSlotMapping {
    std::unordered_map<uint64_t, uint32_t> controlSlots; // s_slotKey(nodeId, controlIndex) -> constants[] slot
    std::unordered_map<uint64_t, uint32_t> inputSlots;   // s_slotKey(nodeId, pinIndex)     -> constants[] slot
    std::unordered_map<uint32_t, uint32_t> textureSlots; // node id -> textures[] slot (unchanged)
};
```

The old `constantSlots` (node id → slot) is subsumed by `controlSlots`. Any current reader of `constantSlots` (the editor's live-write path) migrates to the two new maps.

### 3.4 The sink reads authored channel values

The sink lambda (`MaterialGraphCompiler.cpp:252-269`) currently returns a hardcoded fallback for an unconnected channel. New precedence, per channel:

1. connected → upstream var coerced (unchanged, `:257-259`);
2. else authored → `mapping.inputSlots[s_slotKey(outputNode.id, i)]` read at the field type (`s_poolSwizzle`);
3. else → the existing semantic fallback string (`"0.5"`, `normalize(si.worldNormal)`, …).

Keeping the semantic fallback as the untouched default is what makes `SURFACE_OUTPUT` byte-identical for graphs that author nothing (today's `TestLayer` graphs, §6). Authoring "roughness = 0.6" now writes `defaults.constants[slot] = 0.6` and emits `surf.roughness = <slot>.x;`, with no recompile on later drags — the exact win §10 asked for.

### 3.5 `s_emitNodeExpr` substitution

Substitution order in `s_emitNodeExpr` (`MaterialGraphCompiler.cpp:200-216`):

1. inputs: for each pin, replace `{name}` with the slot-aware `s_resolveInput` (§3.2) — unchanged call site;
2. `{tex}` from `textureSlots` — unchanged;
3. controls: for each `c`, replace `{const<c>}` with `s_poolRef("constants", controlSlots[key])`; replace bare `{const}` with control 0's ref (alias). The empty-template constant fallback (`:204`) becomes `expr = "{const0}"`.

Because a control template supplies its own swizzle (`{const0}.xyz`) but an input template does not, controls substitute the raw `s_poolRef` and inputs substitute `s_poolSwizzle` — the split falls out of which map the value came from.

---

## 4. Pool budget (`GRAPH_MAX_CONSTANTS = 16`)

`constants[]` is a fixed 16-slot array (`GraphInstanceData.h:14`, mirrored `MaterialCommon.glsl:77,81`), and `s_assignResources` already errors past it (`MaterialGraphCompiler.cpp:165-166`). Every authored value now competes for the same 16 slots, so budget pressure is real: a `MIX_VEC3` with all three inputs authored is 3 slots; a fully-authored `SURFACE_OUTPUT` (5 channels) is 5.

Mitigations, in order of preference:

- **Sparse by construction.** Only *touched* inputs get slots (§2); the common graph authors a handful, not all. This is the primary lever.
- **Bake toggle (constant vs variable).** Per [[Material Graph Compiler]] §9, a value the user marks "constant" is baked into the GLSL as a literal (`ResourceKind::NONE` path, no slot, needs a recompile to change). Live values stay slot-backed. Exposing this per authored value lets a graph free slots it never animates. Deferred but the mechanism is the existing baked-literal path.
- **Scalar packing (future).** A `FLOAT` authored value wastes a whole `vec4` slot (`.x` only). Four scalars could co-tenant one slot; the mapping would then carry `(slot, component)`. Noted, not Phase 1.
- **Overflow → Option B.** Hitting 16 is the migration trigger to the variable-length SSBO slice ([[Material System Overhaul]] §6.2, `Material System Overhaul.md:141-145`): each graph declares its own footprint in one shared arena, limit ≈ buffer size. Because the mapping indirection already decouples slot **layout** from generated **logic**, this stays a buffer-layout change, not a compiler rewrite — the same isolation argument the plans already make. This design does not depend on Option B; it just raises the pressure that eventually justifies it.

---

## 5. Runtime write path (no recompile)

A slider drag must be a data write, not a recompile. The chain that already exists:

- `MaterialManager::writeGraphSlot(slot, data)` rewrites the whole `GraphInstanceData` slice (`Material.cpp:98-102`);
- `MaterialInstance::setGraph(graphId, data)` allocates the slot on first call and uploads (`MaterialInstance.cpp:30-42`).

For live edits we do not want `setGraph`'s side effects (it re-ORs `MAT_FLAG_IS_GRAPH`, resets `graphId`, republishes `onMaterialInstanceChanged`, `:37-41`). Add a minimal:

```cpp
void MaterialInstance::updateGraphData(const GraphInstanceData &data); // writeGraphSlot + syncToGPU, no flag/id churn
```

The editor keeps a working copy of the instance's `GraphInstanceData`, looks up `mapping.inputSlots` / `mapping.controlSlots` for the `(nodeId, index)` being dragged, writes the `vec4`, and calls `updateGraphData`. Structural edits (add/remove node or wire) still recompile via `SurfaceGraphManager::registerGraph` + file regen; only value drags take the cheap path. This is the property §10 demanded — "slider drags are data writes, not recompiles" — extended from constant nodes to every authored value.

---

## 6. Migration & impact

- **`GraphNode::constantValue` → `controlValues[0]`.** The only field break. Callers:
  - `TestLayer.cpp` — `s_buildFractTintGraph` and `s_buildSineBandGraph` build ~8 constant nodes via `.constantValue = glm::vec4(...)` (`TestLayer.cpp:39,42,69,74,76,77` and siblings). Each becomes `.controlValues = {glm::vec4(...)}`. Mechanical.
  - `s_assignResources` — its one read of `node->constantValue.v4` (`MaterialGraphCompiler.cpp:169`) moves under the control-values loop.
- **`GraphSlotMapping` shape change** (§3.3). The editor's live-write lookups migrate from `constantSlots[nodeId]` to `controlSlots`/`inputSlots`. No other engine reader of the mapping exists (grep: `SurfaceGraphManager::getMapping` just forwards it, `SurfaceGraphManager.h:48`).
- **Generated file is byte-identical until something is authored.** Existing graphs author zero input values and zero `SURFACE_OUTPUT` channels, so `s_resolveInput` and the sink fall through to the same literals/fallbacks; the constant nodes emit the same `{const}` refs. Regenerating `generated/SurfaceGraphs.glsl` reproduces the current `evalSurface_Graph0`/`Graph1` exactly (`SurfaceGraphs.glsl:12-52`). Rendering is preserved — this is a low-risk staged change, matching the compiler doc's staging philosophy.
- **No GPU/shader/pipeline change.** `GraphInstanceData`, `MaterialCommon.glsl`, and the `GBuffer.fs.glsl` dispatch seam are untouched; authored values reuse `constants[]` slots that already exist.

### Phased order

1. **IR + behavior-preserving compiler.** Add `inputValues`/`controlValues`, migrate `TestLayer` and `s_assignResources` to `controlValues[0]`, add `s_slotKey`/`s_poolSwizzle`, split the mapping. With no authored values present, output is identical. Verify by regenerating the file and diffing against the committed `SurfaceGraphs.glsl`.
2. **Slot-aware resolve + sink.** Wire `s_resolveInput` and the sink lambda to `inputSlots`; extend `s_assignResources` to reserve input/control slots; add `{const<c>}`. First authored value reaches the GPU.
3. **Editor + runtime write.** Serialize `PinView.value`/`NodeControl.value` into the IR on compile; add `MaterialInstance::updateGraphData`; wire slider drags to mapping lookups. Live editing with no recompile.
4. **Budget polish (deferred).** Bake toggle (§9 constant/variable split) and, if 16 is hit, Option B.

---

## 7. Risks / open questions

- **Authored-vs-untouched must be explicit.** The whole budget story rests on only touched inputs getting slots. The sparse `inputValues` map encodes this (absent key = untouched). The editor must only insert a key when the user actually edits the widget, not on node spawn — otherwise every default eats a slot. Flagged for the editor serialization step.
- **`SURFACE_OUTPUT.normal` authored value.** The unconnected default is `normalize(si.worldNormal)`. If a user authors a raw normal vector, should the sink `normalize` it? Probably yes for safety, but that differs from the pass-through the other channels get. Open; lean toward normalizing the normal channel specifically.
- **Scalar slot waste.** One `vec4` per scalar authored value burns the 16-slot budget fast in scalar-heavy graphs. Packing (§4) fixes it but complicates the mapping to `(slot, component)`; defer until measured.
- **Mapping key namespace.** `controlSlots` and `inputSlots` both key on `s_slotKey(nodeId, index)`; they are separate maps so a node's control 0 and input 0 don't collide. Keep them separate rather than one map with a tagged key.
- **Coercion on authored values.** `s_poolSwizzle` handles narrowing a `vec4` slot to the pin type, but an authored value whose declared type is wider than the slot read (e.g. authoring a `VEC4` on a `VEC3` pin) should be validated at author time so the swizzle is always a narrowing. The editor's typed pins (`PinView.type`, `NodeEditorPanel.h:45`) already constrain this.
