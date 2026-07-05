# Material Graph Nodes

**Parent: [[Material Graph Compiler]], [[Material System Overhaul]]. Source of truth: `Engine/src/materials/graph/NodeRegistry.cpp` (`registerBuiltins`).**

Reference for the built-in surface graph nodes. Every node is a `NodeDefinition` (plain data, not code), so the set grows by adding registry entries, never by editing the compiler.

Pins are typed `float`, `int`, `vec2`, `vec3`, or `vec4`. Nodes come in typed variants (`multiply_vec3`, `multiply_float`, ...) so pins line up by type; the compiler only inserts a conversion when you deliberately cross types (a `float` into a `vec3` splats, a `vec4` into a `float` takes `.x`).

---

## Input

### position
Fragment world-space position.

| | Name | Type |
|--------|------|------|
| Output | `out` | vec3 |

### normal
Interpolated world normal, not normalized.

| | Name | Type |
|--------|------|------|
| Output | `out` | vec3 |

### texcoord
Mesh UV coordinates.

| | Name | Type |
|--------|------|------|
| Output | `out` | vec2 |

---

## Value

Each holds a value in the instance pool, editable at runtime with no recompile. One constant slot each.

### constant_float
| | Name | Type |
|--------|------|------|
| Output | `out` | float |

### constant_int
| | Name | Type |
|--------|------|------|
| Output | `out` | int |

### constant_vec3
| | Name | Type |
|--------|------|------|
| Output | `out` | vec3 |

### constant_vec4
| | Name | Type |
|--------|------|------|
| Output | `out` | vec4 |

---

## Texture

### texture_sample
Samples the node's bound texture at a UV. One texture slot, holds the bound texture's bindless index.

| | Name | Type |
|--------|------|------|
| Input | `uv` | vec2 |
| Output | `out` | vec4 |

---

## Vector Math

### multiply_vec3
Component-wise multiply.

| | Name | Type |
|--------|------|------|
| Input | `a` | vec3 |
| Input | `b` | vec3 |
| Output | `out` | vec3 |

### add_vec3
Component-wise add.

| | Name | Type |
|--------|------|------|
| Input | `a` | vec3 |
| Input | `b` | vec3 |
| Output | `out` | vec3 |

### mix_vec3
Linear blend `mix(a, b, t)`, `t` a scalar factor.

| | Name | Type |
|--------|------|------|
| Input | `a` | vec3 |
| Input | `b` | vec3 |
| Input | `t` | float |
| Output | `out` | vec3 |

### fract_vec3
Fractional part, turns a rising value into a repeating 0..1 ramp.

| | Name | Type |
|--------|------|------|
| Input | `a` | vec3 |
| Output | `out` | vec3 |

---

## Scalar Math

### multiply_float
| | Name | Type |
|--------|------|------|
| Input | `a` | float |
| Input | `b` | float |
| Output | `out` | float |

### mix_float
Linear blend `mix(a, b, t)`.

| | Name | Type |
|--------|------|------|
| Input | `a` | float |
| Input | `b` | float |
| Input | `t` | float |
| Output | `out` | float |

---

## Integer Math

### multiply_int
| | Name | Type |
|--------|------|------|
| Input | `a` | int |
| Input | `b` | int |
| Output | `out` | int |

### add_int
| | Name | Type |
|--------|------|------|
| Input | `a` | int |
| Input | `b` | int |
| Output | `out` | int |

---

## Output

### surface_output
The required sink. Each connected pin writes the matching G-buffer field; unconnected pins take a default (albedo white, normal `normalize(si.worldNormal)`, roughness 0.5, metallic 0.0, ao 1.0). The shading model is always `SM_OPENPBR_STANDARD` for now.

| | Name | Type |
|--------|------|------|
| Input | `albedo` | vec3 |
| Input | `normal` | vec3 |
| Input | `roughness` | float |
| Input | `metallic` | float |
| Input | `ao` | float |

---

## Adding a node

Add a `GraphNodeType` enumerator, then one `registerNode({...})` in `registerBuiltins`: its `type`, input/output pins, a `glslTemplate` (the output expression with `{pinName}`, `{tex}`, `{const}` placeholders), and a `resourceKind` if it needs a pool slot. No compiler change. Template rules: [[Material Graph Compiler#4.1 Template placeholder language]].
