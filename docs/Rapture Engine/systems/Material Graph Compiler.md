# Material Graph Compiler

**Related: [[Unified Material Graph]], [[Material System Overhaul]], [[MaterialData]], [[SurfaceGraphManager]]**

Compiles an authored [[MaterialGraph]] into straight-line GLSL. This note describes the
post-overhaul structure (2026-07-10): the compiler is organised around **graph domains**, not a
hardcoded surface sink with two bolted-on variants.

## The domain abstraction

A **graph domain** is a self-contained material family. It owns:

- a **sink node type** (`SURFACE_OUTPUT`, later `TERRAIN_OUTPUT`),
- the name of the **input struct** its generated functions take (`SurfaceInputs`, later
  `TerrainInputs` with tilt/altitude), and
- a list of **passes**.

A **pass** is one generated GLSL function per graph: its output struct (`SurfaceData`), its
dispatcher (`evalSurfaceGraph`), and the target file/guard. A pass declares its **output fields**
(name + type + fallback, plus constant fields like `shadingModelId`); the struct definition, the
per-field assignments, and the dispatcher fallback all derive from that one list.

Every material is one domain's graph. The G-buffer pass and the reduced diffuse pass are two passes
of the **Surface** domain. Terrain becomes a **new domain** registered next to it — its own sink,
its own inputs, its own generated files — with zero compiler edits. That is the whole point of the
overhaul: adding a variation is a data registration, not surgery on `compile()`.

`GraphDomainRegistry` holds domains keyed by sink type. `MaterialGraphCompiler` looks the domain up
from the graph's output-node type; `SurfaceGraphManager` enumerates domains x passes to emit files.

## Codegen phases (one indexed pass over the graph)

`compile()` builds a `GraphContext` **once** and shares it across every phase, so there are no
repeated linear scans (the old code re-scanned all connections per input pin, per node, per
variant):

- `nodeIndex` — node id -> index, replaces linear `findNode`.
- `incoming` — (dstNode,dstPin) -> connection, replaces linear `findInputConnection`.
- `deps` — node -> source node ids, the topo adjacency, built once.
- `usedOutputs` — (srcNode,srcPin) consumed by some wire, so only live outputs get a local.

Phases: resolve domain/sink -> validate (structural + required-texture) -> topo sort from the sink
(cycle + dead-node reporting) -> assign the instance-slice layout (shared by all passes) -> for each
pass, DCE from that pass's output roots and emit one function. The reduced diffuse pass prunes any
node feeding only dropped channels for free, because its topo roots are the smaller field set.

## Output fields vs sink pins

The sink's **input pins** (in [[Material Graph Nodes|NodeRegistry]]) are the channels the author
wires. A pass's **output fields** are the struct the renderer consumes. They match by name; the
binding (field -> sink pin index) is resolved once per compile. A field with no matching sink pin is
a **constant** (e.g. `shadingModelId = SM_OPENPBR_STANDARD`). Domain registration asserts every
bound field has a sink pin of the same type, so the two lists cannot silently drift.

The output structs are **generated** into each pass file (single source of truth) and no longer live
in `MaterialCommon.glsl`. `SurfaceInputs` stays hand-written — it is the input contract each pass
shader fills.
