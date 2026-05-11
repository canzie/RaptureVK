# Procedural Materials System Design

This document outlines the design for a graph-based procedural material system that integrates with the existing `MaterialData` pipeline.

## Overview

The system allows materials to be defined as node graphs rather than static parameter sets. These graphs can be evaluated at runtime (interpreter) or baked to optimized GLSL (generation).

### Goals

1. **Flexibility**: Arbitrary material expressions (noise sampling, math operations, texture blending)
2. **Performance**: Minimal overhead for non-procedural materials, acceptable cost for procedural ones
3. **Iteration speed**: Live editing without shader recompilation (interpreter mode)
4. **Production quality**: Baked GLSL for shipping (generation mode)
5. **Single pipeline**: No pipeline explosion - all materials use the same GBuffer pipeline

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Material Graph                                │
│  (Editor representation - nodes, connections, parameters)               │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Graph Compiler                                   │
│  - Topological sort                                                     │
│  - Register allocation                                                  │
│  - Constant extraction                                                  │
└─────────────────────────────────────────────────────────────────────────┘
                          │                    │
                          ▼                    ▼
          ┌───────────────────────┐  ┌───────────────────────┐
          │   Bytecode Output     │  │    GLSL Output        │
          │   (Interpreter)       │  │    (Baked)            │
          │                       │  │                       │
          │ - 256 byte UBO        │  │ - Generated .glsl     │
          │ - Runtime evaluation  │  │ - Compile-time eval   │
          │ - Live editing        │  │ - Optimal perf        │
          └───────────────────────┘  └───────────────────────┘
                          │                    │
                          ▼                    ▼
          ┌───────────────────────────────────────────────────┐
          │              GBuffer.fs.glsl                      │
          │  - Standard path: existing MaterialData           │
          │  - Procedural path: interpreter OR baked function │
          └───────────────────────────────────────────────────┘
```

---

## Approach 1: Runtime Interpreter

The graph compiles to bytecode stored in a UBO. The shader interprets this at runtime.

### Memory Layout

```cpp
// 256 bytes per procedural material - cache-friendly
struct ProceduralMaterialData {
    // Constants pool - literal values from graph nodes
    vec4 constants[6];          // 96 bytes (24 floats)

    // Bindless texture indices for sampling
    uint32_t textures[4];       // 16 bytes (4 textures max)

    // Output register mapping (which register feeds each output)
    uint8_t albedoReg;          // 1 byte
    uint8_t normalReg;          // 1 byte
    uint8_t roughnessReg;       // 1 byte
    uint8_t metallicReg;        // 1 byte
    uint8_t aoReg;              // 1 byte
    uint8_t emissiveReg;        // 1 byte
    uint8_t opCount;            // 1 byte (max 16)
    uint8_t flags;              // 1 byte

    // Instruction stream: 8 bytes each, max 16 operations
    uint32_t instructions[32];  // 128 bytes

    // Padding to 256 bytes
    uint32_t _padding[2];       // 8 bytes
};
// Total: 96 + 16 + 8 + 128 + 8 = 256 bytes
```

### Instruction Encoding

Each instruction is 8 bytes (2 x uint32):

```
Word 0 (bits):
  [31:24] opcode          - operation to perform
  [23:20] outputReg       - destination register (0-7)
  [19:16] outputSwizzle   - which components to write
  [15:12] srcA_type       - source A type (const/reg/uv/etc)
  [11:8]  srcA_index      - source A index
  [7:4]   srcA_swizzle    - source A component selection
  [3:0]   reserved

Word 1 (bits):
  [31:28] srcB_type
  [27:24] srcB_index
  [23:20] srcB_swizzle
  [19:16] srcC_type       - for 3-operand ops (mix, clamp, mad)
  [15:12] srcC_index
  [11:8]  srcC_swizzle
  [7:0]   flags/reserved
```

### Source Types

```cpp
enum class SourceType : uint8_t {
    Constant    = 0,  // constants[index]
    Register    = 1,  // regs[index]
    UV          = 2,  // texcoord input
    Position    = 3,  // world position
    Normal      = 4,  // world normal
    Zero        = 5,  // vec4(0.0)
    One         = 6,  // vec4(1.0)
    Time        = 7,  // animation time (future)
};
```

### Operation Codes

```cpp
enum class OpCode : uint8_t {
    Nop         = 0,
    Mov         = 1,   // out = A
    Add         = 2,   // out = A + B
    Sub         = 3,   // out = A - B
    Mul         = 4,   // out = A * B
    Div         = 5,   // out = A / B
    Mad         = 6,   // out = A * B + C
    Mix         = 7,   // out = mix(A, B, C)
    Clamp       = 8,   // out = clamp(A, B, C)
    Min         = 9,   // out = min(A, B)
    Max         = 10,  // out = max(A, B)
    Dot         = 11,  // out = vec4(dot(A.xyz, B.xyz))
    Normalize   = 12,  // out = vec4(normalize(A.xyz), 0)
    Pow         = 13,  // out = pow(A, B)
    Sqrt        = 14,  // out = sqrt(A)
    Abs         = 15,  // out = abs(A)
    Fract       = 16,  // out = fract(A)
    Floor       = 17,  // out = floor(A)
    Sample      = 18,  // out = texture(tex[A.x], B.xy)
    SampleLod   = 19,  // out = textureLod(tex[A.x], B.xy, C.x)
    OneMinus    = 20,  // out = 1.0 - A
    Saturate    = 21,  // out = clamp(A, 0, 1)
    Sin         = 22,  // out = sin(A)
    Cos         = 23,  // out = cos(A)
    Lerp        = 24,  // alias for Mix
    SmoothStep  = 25,  // out = smoothstep(A, B, C)
};
```

### GLSL Evaluator

```glsl
// ProceduralMaterial.glsl

#ifndef PROCEDURAL_MATERIAL_GLSL
#define PROCEDURAL_MATERIAL_GLSL

#define PROC_MAX_OPS 16
#define PROC_MAX_REGS 8

// Source type constants
#define SRC_CONST    0u
#define SRC_REG      1u
#define SRC_UV       2u
#define SRC_POSITION 3u
#define SRC_NORMAL   4u
#define SRC_ZERO     5u
#define SRC_ONE      6u

// Opcode constants
#define OP_NOP       0u
#define OP_MOV       1u
#define OP_ADD       2u
#define OP_SUB       3u
#define OP_MUL       4u
#define OP_DIV       5u
#define OP_MAD       6u
#define OP_MIX       7u
#define OP_CLAMP     8u
#define OP_MIN       9u
#define OP_MAX       10u
#define OP_DOT       11u
#define OP_NORMALIZE 12u
#define OP_POW       13u
#define OP_SQRT      14u
#define OP_ABS       15u
#define OP_FRACT     16u
#define OP_FLOOR     17u
#define OP_SAMPLE    18u
#define OP_SAMPLE_LOD 19u
#define OP_ONE_MINUS 20u
#define OP_SATURATE  21u
#define OP_SIN       22u
#define OP_COS       23u
#define OP_SMOOTHSTEP 25u

struct ProceduralMaterialData {
    vec4 constants[6];
    uvec4 textures;
    uint outputMapping;
    uint opCountAndFlags;
    uvec2 instructions[PROC_MAX_OPS];
};

vec4 procFetchSource(
    uint srcType,
    uint srcIdx,
    vec4 regs[PROC_MAX_REGS],
    vec4 constants[6],
    vec2 uv,
    vec3 position,
    vec3 normal
) {
    if (srcType == SRC_CONST)    return constants[srcIdx];
    if (srcType == SRC_REG)      return regs[srcIdx];
    if (srcType == SRC_UV)       return vec4(uv, 0.0, 0.0);
    if (srcType == SRC_POSITION) return vec4(position, 0.0);
    if (srcType == SRC_NORMAL)   return vec4(normal, 0.0);
    if (srcType == SRC_ZERO)     return vec4(0.0);
    if (srcType == SRC_ONE)      return vec4(1.0);
    return vec4(0.0);
}

void evalProceduralMaterial(
    ProceduralMaterialData proc,
    sampler2D texArray[],
    vec2 uv,
    vec3 position,
    vec3 normal,
    out vec3 outAlbedo,
    out vec3 outNormal,
    out float outRoughness,
    out float outMetallic,
    out float outAO
) {
    vec4 regs[PROC_MAX_REGS];

    // Zero-initialize registers
    for (int i = 0; i < PROC_MAX_REGS; i++) {
        regs[i] = vec4(0.0);
    }

    uint opCount = proc.opCountAndFlags & 0xFFu;

    // Evaluation loop - all threads execute same instructions (uniform control flow)
    for (uint i = 0u; i < opCount; i++) {
        uvec2 instr = proc.instructions[i];

        // Decode instruction
        uint opcode   = (instr.x >> 24u) & 0xFFu;
        uint outReg   = (instr.x >> 20u) & 0xFu;
        uint srcAType = (instr.x >> 12u) & 0xFu;
        uint srcAIdx  = (instr.x >> 8u)  & 0xFu;
        uint srcBType = (instr.y >> 28u) & 0xFu;
        uint srcBIdx  = (instr.y >> 24u) & 0xFu;
        uint srcCType = (instr.y >> 16u) & 0xFu;
        uint srcCIdx  = (instr.y >> 12u) & 0xFu;

        // Fetch source values
        vec4 A = procFetchSource(srcAType, srcAIdx, regs, proc.constants, uv, position, normal);
        vec4 B = procFetchSource(srcBType, srcBIdx, regs, proc.constants, uv, position, normal);
        vec4 C = procFetchSource(srcCType, srcCIdx, regs, proc.constants, uv, position, normal);

        vec4 result = vec4(0.0);

        // Execute operation
        switch (opcode) {
            case OP_MOV:       result = A; break;
            case OP_ADD:       result = A + B; break;
            case OP_SUB:       result = A - B; break;
            case OP_MUL:       result = A * B; break;
            case OP_DIV:       result = A / max(B, vec4(0.0001)); break;
            case OP_MAD:       result = A * B + C; break;
            case OP_MIX:       result = mix(A, B, C); break;
            case OP_CLAMP:     result = clamp(A, B, C); break;
            case OP_MIN:       result = min(A, B); break;
            case OP_MAX:       result = max(A, B); break;
            case OP_DOT:       result = vec4(dot(A.xyz, B.xyz)); break;
            case OP_NORMALIZE: result = vec4(normalize(A.xyz), 0.0); break;
            case OP_POW:       result = pow(max(A, vec4(0.0)), B); break;
            case OP_SQRT:      result = sqrt(max(A, vec4(0.0))); break;
            case OP_ABS:       result = abs(A); break;
            case OP_FRACT:     result = fract(A); break;
            case OP_FLOOR:     result = floor(A); break;
            case OP_SAMPLE: {
                uint texIdx = proc.textures[uint(A.x) & 3u];
                result = texture(texArray[texIdx], B.xy);
                break;
            }
            case OP_SAMPLE_LOD: {
                uint texIdx = proc.textures[uint(A.x) & 3u];
                result = textureLod(texArray[texIdx], B.xy, C.x);
                break;
            }
            case OP_ONE_MINUS: result = vec4(1.0) - A; break;
            case OP_SATURATE:  result = clamp(A, vec4(0.0), vec4(1.0)); break;
            case OP_SIN:       result = sin(A); break;
            case OP_COS:       result = cos(A); break;
            case OP_SMOOTHSTEP: result = smoothstep(A, B, C); break;
        }

        regs[outReg] = result;
    }

    // Extract outputs from registers based on mapping
    uint mapping = proc.outputMapping;
    outAlbedo    = regs[(mapping >> 0u)  & 0xFu].rgb;
    outNormal    = regs[(mapping >> 4u)  & 0xFu].xyz;
    outRoughness = regs[(mapping >> 8u)  & 0xFu].x;
    outMetallic  = regs[(mapping >> 12u) & 0xFu].x;
    outAO        = regs[(mapping >> 16u) & 0xFu].x;
}

#endif // PROCEDURAL_MATERIAL_GLSL
```

### Performance Characteristics

**Why this doesn't kill performance:**

| Factor | Analysis |
|--------|----------|
| Loop uniformity | All fragments using same material execute identical instruction sequence |
| Branch uniformity | Switch case taken is uniform across wave/warp |
| Register pressure | 8 vec4 = 32 floats, well within limits |
| Dynamic indexing | `regs[idx]` is handled efficiently by modern GPUs |

**Expected overhead vs baked shader:** ~1.5-2x for procedural materials.

**When to use:** Development, live editing, infrequently-used materials.

---

## Approach 2: GLSL Generation (Baking)

The graph compiles to straight-line GLSL code. No loop, no switch, no interpreter overhead.

### Generated Code Structure

All baked materials go into a single include file:

```glsl
// ProceduralMaterialsBaked.glsl - GENERATED FILE, DO NOT EDIT
// Generated: 2025-01-15 14:32:00
// Source graphs: assets/materials/*.matgraph

#ifndef PROCEDURAL_MATERIALS_BAKED_GLSL
#define PROCEDURAL_MATERIALS_BAKED_GLSL

// Material IDs (must match C++ ProceduralMaterialID enum)
#define PROC_MAT_ROCKY_GROUND    0
#define PROC_MAT_LAVA_ROCK       1
#define PROC_MAT_MOSSY_STONE     2
#define PROC_MAT_WEATHERED_METAL 3

// ============================================================================
// Material: RockyGround
// Source: assets/materials/rocky_ground.matgraph
// ============================================================================
void evalMaterial_RockyGround(
    vec4 params[6],           // constants from ProceduralMaterialData
    uvec4 texIndices,         // texture bindless indices
    sampler2D texArray[],
    vec2 uv,
    vec3 position,
    vec3 normal,
    out vec3 outAlbedo,
    out vec3 outNormal,
    out float outRoughness,
    out float outMetallic,
    out float outAO
) {
    // params[0].x = noiseScale
    // params[0].y = displacementMul
    // params[1]   = tintColor

    vec4 _n0 = texture(texArray[texIndices.x], uv);                    // Sample albedo
    vec4 _n1 = texture(texArray[texIndices.y], uv * params[0].x);     // Sample noise
    float _n2 = clamp(_n1.r * params[0].y, 0.0, 1.0);                 // Clamp displacement
    vec3 _n3 = _n0.rgb * params[1].rgb;                               // Tint albedo
    vec4 _n4 = texture(texArray[texIndices.z], uv);                   // Sample normal map
    vec3 _n5 = _n4.xyz * 2.0 - 1.0;                                   // Unpack normal
    float _n6 = mix(0.3, 0.9, _n2);                                   // Roughness from displacement

    outAlbedo = _n3;
    outNormal = _n5;
    outRoughness = _n6;
    outMetallic = 0.0;
    outAO = 1.0;
}

// ============================================================================
// Material: LavaRock
// Source: assets/materials/lava_rock.matgraph
// ============================================================================
void evalMaterial_LavaRock(
    vec4 params[6],
    uvec4 texIndices,
    sampler2D texArray[],
    vec2 uv,
    vec3 position,
    vec3 normal,
    out vec3 outAlbedo,
    out vec3 outNormal,
    out float outRoughness,
    out float outMetallic,
    out float outAO
) {
    // params[0].x = glowIntensity
    // params[0].y = crackScale

    vec4 _n0 = texture(texArray[texIndices.x], uv);                   // Base rock
    vec4 _n1 = texture(texArray[texIndices.y], uv * params[0].y);    // Crack mask
    vec3 _n2 = mix(_n0.rgb, vec3(1.0, 0.3, 0.0), _n1.r * params[0].x); // Glow in cracks

    outAlbedo = _n2;
    outNormal = vec3(0.0, 0.0, 1.0);
    outRoughness = mix(0.8, 0.2, _n1.r);
    outMetallic = 0.0;
    outAO = 1.0 - _n1.r * 0.5;
}

// ============================================================================
// Dispatcher - call appropriate material function based on ID
// ============================================================================
void evalBakedProceduralMaterial(
    uint materialID,
    vec4 params[6],
    uvec4 texIndices,
    sampler2D texArray[],
    vec2 uv,
    vec3 position,
    vec3 normal,
    out vec3 outAlbedo,
    out vec3 outNormal,
    out float outRoughness,
    out float outMetallic,
    out float outAO
) {
    switch (materialID) {
        case PROC_MAT_ROCKY_GROUND:
            evalMaterial_RockyGround(params, texIndices, texArray, uv, position, normal,
                outAlbedo, outNormal, outRoughness, outMetallic, outAO);
            break;
        case PROC_MAT_LAVA_ROCK:
            evalMaterial_LavaRock(params, texIndices, texArray, uv, position, normal,
                outAlbedo, outNormal, outRoughness, outMetallic, outAO);
            break;
        // ... more materials
        default:
            outAlbedo = vec3(1.0, 0.0, 1.0); // Magenta error
            outNormal = vec3(0.0, 0.0, 1.0);
            outRoughness = 0.5;
            outMetallic = 0.0;
            outAO = 1.0;
            break;
    }
}

#endif // PROCEDURAL_MATERIALS_BAKED_GLSL
```

### Comparison: Interpreter vs Generated

The same graph produces different outputs:

**Interpreter output (bytecode):**
```cpp
ProceduralMaterialData data;
data.constants[0] = vec4(4.0, 2.0, 0.0, 0.0);  // noiseScale, displacementMul
data.constants[1] = vec4(1.0, 0.9, 0.8, 1.0);  // tintColor
data.textures = {albedoIdx, noiseIdx, normalIdx, 0};
data.opCount = 7;
data.instructions = {
    encode(OP_SAMPLE, R0, CONST:0, UV),           // R0 = sample albedo
    encode(OP_SAMPLE, R1, CONST:1, UV*scale),     // R1 = sample noise
    encode(OP_CLAMP, R2, R1, ZERO, ONE),          // R2 = clamp
    encode(OP_MUL, R3, R0, CONST:2),              // R3 = tinted albedo
    // ...
};
```

**Generated output (GLSL):**
```glsl
vec4 _n0 = texture(texArray[texIndices.x], uv);
vec4 _n1 = texture(texArray[texIndices.y], uv * params[0].x);
float _n2 = clamp(_n1.r * params[0].y, 0.0, 1.0);
vec3 _n3 = _n0.rgb * params[1].rgb;
// ...
```

**Key differences:**

| Aspect | Interpreter | Generated |
|--------|-------------|-----------|
| Runtime cost | Loop + switch overhead | Direct execution |
| Shader compilation | None (data only) | Required on bake |
| Live editing | Yes | No (requires rebake) |
| Code size | Fixed evaluator | Grows with material count |
| Optimization | Limited | Full GLSL compiler optimization |

---

## Hybrid Approach: Development + Production

The recommended workflow combines both approaches:

### Workflow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         DEVELOPMENT MODE                                 │
│                                                                         │
│  Edit graph ──► Compile to bytecode ──► Upload to UBO ──► See result   │
│       │                                                                 │
│       └──────────────────── instant iteration ─────────────────────────│
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ User clicks "Bake Material"
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         BAKE PROCESS                                     │
│                                                                         │
│  1. Generate GLSL function for this material                            │
│  2. Add to ProceduralMaterialsBaked.glsl                                │
│  3. Trigger shader recompilation                                        │
│  4. Mark material as "baked" in asset database                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         PRODUCTION MODE                                  │
│                                                                         │
│  All procedural materials use baked GLSL                                │
│  Interpreter still available as fallback / for dynamic materials        │
└─────────────────────────────────────────────────────────────────────────┘
```

### GBuffer Integration

```glsl
// GBuffer.fs.glsl

#include "common/MaterialCommon.glsl"
#include "common/ProceduralMaterial.glsl"        // Interpreter
#include "generated/ProceduralMaterialsBaked.glsl" // Baked functions

// Flag in MaterialData.flags
#define MAT_FLAG_IS_PROCEDURAL      (1u << 20)
#define MAT_FLAG_PROCEDURAL_BAKED   (1u << 21)

// Procedural material data buffer (binding 1)
layout(set = 1, binding = 1) uniform ProceduralMaterialBuffer {
    ProceduralMaterialData data;
} u_proceduralMaterials[];

void main() {
    MaterialData mat = u_materials[inMaterialIndex].data;
    uint flags = mat.flags | inFlags;

    vec3 albedo;
    vec3 materialNormal;
    float roughness, metallic, ao;

    if (matHasFlag(flags, MAT_FLAG_IS_PROCEDURAL)) {
        // Get procedural material data
        // proceduralIndex stored in unused field or separate component
        uint procIndex = mat.texIndices1.w;  // repurpose splatMap slot
        ProceduralMaterialData proc = u_proceduralMaterials[procIndex].data;

        if (matHasFlag(flags, MAT_FLAG_PROCEDURAL_BAKED)) {
            // Use baked GLSL - dispatch to generated function
            uint bakedID = proc.opCountAndFlags >> 8u;  // material type ID
            evalBakedProceduralMaterial(
                bakedID,
                proc.constants,
                proc.textures,
                u_textures,
                inTexCoord,
                inFragPosDepth.xyz,
                inNormal,
                albedo, materialNormal, roughness, metallic, ao
            );
        } else {
            // Use interpreter - runtime evaluation
            evalProceduralMaterial(
                proc,
                u_textures,
                inTexCoord,
                inFragPosDepth.xyz,
                inNormal,
                albedo, materialNormal, roughness, metallic, ao
            );
        }

        // Transform material normal to world space
        mat3 TBN = mat3(
            normalize(inTangent),
            normalize(inBitangent),
            normalize(inNormal)
        );
        normal = normalize(TBN * materialNormal);

    } else {
        // Standard material path - unchanged
        vec2 texCoord = mix(vec2(0.0), inTexCoord, matFlagMul(flags, MAT_FLAG_HAS_TEXCOORDS));
        albedo = SAMPLE_ALBEDO(mat, u_textures, texCoord);
        roughness = SAMPLE_ROUGHNESS(mat, u_textures, texCoord);
        metallic = SAMPLE_METALLIC(mat, u_textures, texCoord);
        ao = SAMPLE_AO(mat, u_textures, texCoord);

        // Normal map handling (existing code)
        // ...
    }

    // Output to GBuffer
    gPositionDepth = inFragPosDepth;
    gNormal = vec4(normal, 1.0);
    gAlbedoSpec = vec4(albedo, 1.0);
    gMaterial = vec4(metallic, roughness, ao, 1.0);
}
```

---

## C++ Graph Compiler

### Graph Data Structures

```cpp
// MaterialGraph.h

namespace Rapture {

enum class PinType : uint8_t {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Int,
    UInt,
    BindlessTexture,
};

enum class NodeType : uint8_t {
    // Inputs
    Constant,
    TexCoordInput,
    PositionInput,
    NormalInput,
    TimeInput,

    // Texture
    TextureSample,
    TextureSampleLOD,

    // Math
    Add,
    Subtract,
    Multiply,
    Divide,
    MultiplyAdd,

    // Interpolation
    Mix,
    SmoothStep,

    // Range
    Clamp,
    Saturate,
    Min,
    Max,
    Abs,

    // Functions
    Pow,
    Sqrt,
    Sin,
    Cos,
    Floor,
    Fract,
    OneMinus,

    // Vector
    Dot,
    Normalize,

    // Swizzle
    SplitChannels,
    CombineChannels,

    // Output
    MaterialOutput,
};

struct NodePin {
    std::string name;
    PinType type;
    uint32_t nodeId;      // owning node
    uint32_t pinIndex;    // index within node's inputs/outputs
};

struct NodeConnection {
    uint32_t sourceNode;
    uint32_t sourcePin;
    uint32_t destNode;
    uint32_t destPin;
};

struct GraphNode {
    uint32_t id;
    NodeType type;
    std::string name;
    glm::vec2 editorPosition;  // for UI

    std::vector<NodePin> inputs;
    std::vector<NodePin> outputs;

    // Node-specific data
    glm::vec4 constantValue;        // for Constant nodes
    uint32_t textureBindlessIndex;  // for TextureSample nodes
};

struct MaterialGraph {
    std::string name;
    std::string sourcePath;

    std::vector<GraphNode> nodes;
    std::vector<NodeConnection> connections;

    // Output node ID
    uint32_t outputNodeId;

    // Exposed parameters (can be changed without recompile)
    struct ExposedParam {
        std::string name;
        uint32_t nodeId;      // which Constant node
        PinType type;
        glm::vec4 defaultValue;
        glm::vec4 minValue;
        glm::vec4 maxValue;
    };
    std::vector<ExposedParam> exposedParams;
};

} // namespace Rapture
```

### Compiler Implementation

```cpp
// MaterialGraphCompiler.h

namespace Rapture {

struct CompileResult {
    bool success;
    std::string errorMessage;

    // Bytecode output (for interpreter)
    ProceduralMaterialData bytecode;

    // GLSL output (for baking)
    std::string glslFunction;
    std::string glslFunctionName;
    uint32_t bakedMaterialID;
};

class MaterialGraphCompiler {
public:
    CompileResult compile(const MaterialGraph& graph);

    // Compile all graphs and generate the baked GLSL file
    bool generateBakedFile(
        const std::vector<MaterialGraph>& graphs,
        const std::string& outputPath
    );

private:
    struct CompiledNode {
        uint32_t nodeId;
        uint8_t outputRegister;
        std::string glslVarName;
    };

    // Topological sort for execution order
    std::vector<uint32_t> topologicalSort(const MaterialGraph& graph);

    // Register allocation
    uint8_t allocateRegister();
    void freeRegister(uint8_t reg);

    // Bytecode emission
    uint64_t encodeInstruction(
        OpCode op,
        uint8_t outReg,
        SourceType srcAType, uint8_t srcAIdx,
        SourceType srcBType, uint8_t srcBIdx,
        SourceType srcCType = SourceType::Zero, uint8_t srcCIdx = 0
    );

    // GLSL emission
    std::string emitGLSLNode(
        const GraphNode& node,
        const std::map<uint32_t, CompiledNode>& compiledNodes,
        const MaterialGraph& graph
    );

    // Helpers
    SourceType getSourceType(const NodeConnection& conn, const MaterialGraph& graph);
    uint8_t getSourceIndex(const NodeConnection& conn,
                           const std::map<uint32_t, CompiledNode>& compiledNodes);

    // State
    std::array<bool, 8> m_registerUsage;
    uint8_t m_nextRegister = 0;

    std::vector<glm::vec4> m_constants;
    std::map<uint32_t, uint8_t> m_nodeToConstantIndex;

    std::vector<uint32_t> m_textureIndices;
    std::map<uint32_t, uint8_t> m_nodeToTextureIndex;
};

} // namespace Rapture
```

### Compiler Core Logic

```cpp
// MaterialGraphCompiler.cpp

CompileResult MaterialGraphCompiler::compile(const MaterialGraph& graph) {
    CompileResult result;
    result.success = false;

    // Reset state
    m_registerUsage.fill(false);
    m_nextRegister = 0;
    m_constants.clear();
    m_nodeToConstantIndex.clear();
    m_textureIndices.clear();
    m_nodeToTextureIndex.clear();

    // Validate graph
    if (graph.outputNodeId == 0) {
        result.errorMessage = "Graph has no output node";
        return result;
    }

    // Topological sort
    auto sortedNodes = topologicalSort(graph);
    if (sortedNodes.empty()) {
        result.errorMessage = "Graph contains cycles or is empty";
        return result;
    }

    // First pass: collect constants and textures
    for (uint32_t nodeId : sortedNodes) {
        const auto& node = findNode(graph, nodeId);

        if (node.type == NodeType::Constant) {
            m_nodeToConstantIndex[nodeId] = static_cast<uint8_t>(m_constants.size());
            m_constants.push_back(node.constantValue);
        }
        else if (node.type == NodeType::TextureSample ||
                 node.type == NodeType::TextureSampleLOD) {
            m_nodeToTextureIndex[nodeId] = static_cast<uint8_t>(m_textureIndices.size());
            m_textureIndices.push_back(node.textureBindlessIndex);
        }
    }

    // Check limits
    if (m_constants.size() > 6) {
        result.errorMessage = "Too many constants (max 6)";
        return result;
    }
    if (m_textureIndices.size() > 4) {
        result.errorMessage = "Too many textures (max 4)";
        return result;
    }

    // Second pass: compile nodes
    std::map<uint32_t, CompiledNode> compiledNodes;
    std::vector<uint64_t> instructions;
    std::stringstream glsl;

    int glslVarCounter = 0;

    for (uint32_t nodeId : sortedNodes) {
        const auto& node = findNode(graph, nodeId);

        // Skip input nodes (they don't generate instructions)
        if (node.type == NodeType::TexCoordInput ||
            node.type == NodeType::PositionInput ||
            node.type == NodeType::NormalInput ||
            node.type == NodeType::Constant ||
            node.type == NodeType::MaterialOutput) {
            continue;
        }

        CompiledNode compiled;
        compiled.nodeId = nodeId;
        compiled.outputRegister = allocateRegister();
        compiled.glslVarName = "_n" + std::to_string(glslVarCounter++);

        // Generate bytecode instruction
        uint64_t instr = emitBytecodeForNode(node, compiledNodes, graph);
        instructions.push_back(instr);

        // Generate GLSL line
        std::string glslLine = emitGLSLNode(node, compiledNodes, graph);
        glsl << "    " << getGLSLType(node) << " " << compiled.glslVarName
             << " = " << glslLine << ";\n";

        compiledNodes[nodeId] = compiled;
    }

    // Check instruction limit
    if (instructions.size() > 16) {
        result.errorMessage = "Too many operations (max 16)";
        return result;
    }

    // Build output mapping
    const auto& outputNode = findNode(graph, graph.outputNodeId);
    uint32_t outputMapping = 0;

    auto mapOutput = [&](const std::string& pinName, uint32_t shift) {
        auto conn = findConnectionToPin(graph, graph.outputNodeId, pinName);
        if (conn) {
            uint8_t reg = compiledNodes[conn->sourceNode].outputRegister;
            outputMapping |= (reg << shift);
        }
    };

    mapOutput("albedo", 0);
    mapOutput("normal", 4);
    mapOutput("roughness", 8);
    mapOutput("metallic", 12);
    mapOutput("ao", 16);

    // Pack bytecode result
    ProceduralMaterialData& bytecode = result.bytecode;
    memset(&bytecode, 0, sizeof(bytecode));

    for (size_t i = 0; i < m_constants.size(); i++) {
        bytecode.constants[i] = m_constants[i];
    }
    for (size_t i = 0; i < m_textureIndices.size(); i++) {
        bytecode.textures[i] = m_textureIndices[i];
    }
    bytecode.outputMapping = outputMapping;
    bytecode.opCountAndFlags = static_cast<uint32_t>(instructions.size());

    for (size_t i = 0; i < instructions.size(); i++) {
        bytecode.instructions[i * 2 + 0] = static_cast<uint32_t>(instructions[i] & 0xFFFFFFFF);
        bytecode.instructions[i * 2 + 1] = static_cast<uint32_t>(instructions[i] >> 32);
    }

    // Build GLSL function
    std::stringstream glslFunc;
    glslFunc << "void evalMaterial_" << sanitizeName(graph.name) << "(\n";
    glslFunc << "    vec4 params[6],\n";
    glslFunc << "    uvec4 texIndices,\n";
    glslFunc << "    sampler2D texArray[],\n";
    glslFunc << "    vec2 uv,\n";
    glslFunc << "    vec3 position,\n";
    glslFunc << "    vec3 normal,\n";
    glslFunc << "    out vec3 outAlbedo,\n";
    glslFunc << "    out vec3 outNormal,\n";
    glslFunc << "    out float outRoughness,\n";
    glslFunc << "    out float outMetallic,\n";
    glslFunc << "    out float outAO\n";
    glslFunc << ") {\n";
    glslFunc << glsl.str();
    glslFunc << "\n";
    glslFunc << "    outAlbedo = " << getOutputVar(compiledNodes, graph, "albedo") << ".rgb;\n";
    glslFunc << "    outNormal = " << getOutputVar(compiledNodes, graph, "normal") << ".xyz;\n";
    glslFunc << "    outRoughness = " << getOutputVar(compiledNodes, graph, "roughness") << ".x;\n";
    glslFunc << "    outMetallic = " << getOutputVar(compiledNodes, graph, "metallic") << ".x;\n";
    glslFunc << "    outAO = " << getOutputVar(compiledNodes, graph, "ao") << ".x;\n";
    glslFunc << "}\n";

    result.glslFunction = glslFunc.str();
    result.glslFunctionName = "evalMaterial_" + sanitizeName(graph.name);
    result.success = true;

    return result;
}

std::string MaterialGraphCompiler::emitGLSLNode(
    const GraphNode& node,
    const std::map<uint32_t, CompiledNode>& compiledNodes,
    const MaterialGraph& graph
) {
    auto getInput = [&](const std::string& pinName) -> std::string {
        auto conn = findConnectionToPin(graph, node.id, pinName);
        if (!conn) {
            // Check for default value on pin
            return "vec4(0.0)";
        }

        const auto& srcNode = findNode(graph, conn->sourceNode);

        // Handle special source nodes
        if (srcNode.type == NodeType::Constant) {
            uint8_t idx = m_nodeToConstantIndex.at(srcNode.id);
            return "params[" + std::to_string(idx) + "]";
        }
        if (srcNode.type == NodeType::TexCoordInput) {
            return "vec4(uv, 0.0, 0.0)";
        }
        if (srcNode.type == NodeType::PositionInput) {
            return "vec4(position, 0.0)";
        }
        if (srcNode.type == NodeType::NormalInput) {
            return "vec4(normal, 0.0)";
        }

        return compiledNodes.at(conn->sourceNode).glslVarName;
    };

    switch (node.type) {
        case NodeType::Add:
            return getInput("a") + " + " + getInput("b");

        case NodeType::Subtract:
            return getInput("a") + " - " + getInput("b");

        case NodeType::Multiply:
            return getInput("a") + " * " + getInput("b");

        case NodeType::Divide:
            return getInput("a") + " / max(" + getInput("b") + ", vec4(0.0001))";

        case NodeType::Mix:
            return "mix(" + getInput("a") + ", " + getInput("b") + ", " + getInput("t") + ")";

        case NodeType::Clamp:
            return "clamp(" + getInput("value") + ", " + getInput("min") + ", " + getInput("max") + ")";

        case NodeType::Saturate:
            return "clamp(" + getInput("value") + ", vec4(0.0), vec4(1.0))";

        case NodeType::TextureSample: {
            uint8_t texIdx = m_nodeToTextureIndex.at(node.id);
            return "texture(texArray[texIndices[" + std::to_string(texIdx) + "]], "
                   + getInput("uv") + ".xy)";
        }

        case NodeType::Pow:
            return "pow(max(" + getInput("base") + ", vec4(0.0)), " + getInput("exp") + ")";

        case NodeType::Sqrt:
            return "sqrt(max(" + getInput("value") + ", vec4(0.0)))";

        case NodeType::OneMinus:
            return "vec4(1.0) - " + getInput("value");

        case NodeType::Normalize:
            return "vec4(normalize(" + getInput("value") + ".xyz), 0.0)";

        case NodeType::Dot:
            return "vec4(dot(" + getInput("a") + ".xyz, " + getInput("b") + ".xyz))";

        // ... etc for all node types

        default:
            return "vec4(1.0, 0.0, 1.0, 1.0)"; // Error magenta
    }
}
```

---

## Serialization

### Graph File Format (.matgraph)

JSON-based for readability and easy editing:

```json
{
    "name": "RockyGround",
    "version": 1,
    "nodes": [
        {
            "id": 1,
            "type": "TexCoordInput",
            "position": [100, 200]
        },
        {
            "id": 2,
            "type": "Constant",
            "position": [100, 300],
            "value": [4.0, 0.0, 0.0, 0.0]
        },
        {
            "id": 3,
            "type": "TextureSample",
            "position": [300, 200],
            "textureAsset": "textures/rock_albedo.ktx2"
        },
        {
            "id": 4,
            "type": "TextureSample",
            "position": [300, 300],
            "textureAsset": "textures/noise_perlin.ktx2"
        },
        {
            "id": 5,
            "type": "Multiply",
            "position": [250, 350]
        },
        {
            "id": 6,
            "type": "Clamp",
            "position": [450, 300]
        },
        {
            "id": 7,
            "type": "MaterialOutput",
            "position": [600, 250]
        }
    ],
    "connections": [
        {"from": [1, 0], "to": [3, 0]},
        {"from": [1, 0], "to": [5, 0]},
        {"from": [2, 0], "to": [5, 1]},
        {"from": [5, 0], "to": [4, 0]},
        {"from": [4, 0], "to": [6, 0]},
        {"from": [3, 0], "to": [7, 0]},
        {"from": [6, 0], "to": [7, 2]}
    ],
    "exposedParams": [
        {
            "name": "Noise Scale",
            "nodeId": 2,
            "default": [4.0, 0.0, 0.0, 0.0],
            "min": [0.1, 0.0, 0.0, 0.0],
            "max": [20.0, 0.0, 0.0, 0.0]
        }
    ],
    "outputNode": 7
}
```

---

## Usage Summary

### Development Workflow

1. Create/edit material graph in editor
2. Graph auto-compiles to bytecode on change
3. See results immediately (interpreter mode)
4. Iterate rapidly

### Production Workflow

1. Select materials to bake
2. Click "Bake Materials" in editor
3. System generates `ProceduralMaterialsBaked.glsl`
4. Shader recompiles
5. Materials now use optimized baked path

### Runtime Behavior

```cpp
// When loading a material
void MaterialSystem::loadMaterial(const std::string& path) {
    MaterialGraph graph = loadGraph(path);

    auto result = m_compiler.compile(graph);

    // Always have bytecode available
    ProceduralMaterialData procData = result.bytecode;

    // Check if baked version exists
    if (m_bakedMaterialIDs.contains(graph.name)) {
        procData.opCountAndFlags |= (m_bakedMaterialIDs[graph.name] << 8);
        procData.opCountAndFlags |= PROC_FLAG_BAKED;
    }

    // Upload to GPU
    uploadProceduralMaterial(procData);
}
```

---

## Future Extensions

### Potential Additions

1. **Vertex displacement**: Output to vertex shader for displacement mapping
2. **Animated materials**: Time input for animated effects
3. **Conditional nodes**: If/else based on material parameters
4. **Custom functions**: User-defined GLSL snippets as nodes
5. **Sub-graphs**: Reusable node groups
6. **LOD variants**: Different graphs for distance-based quality

### Performance Optimizations

1. **Dead code elimination**: Remove unused nodes during compilation
2. **Constant folding**: Evaluate constant expressions at compile time
3. **Common subexpression elimination**: Reuse identical calculations
4. **Texture fetch coalescing**: Group texture samples for better cache usage
