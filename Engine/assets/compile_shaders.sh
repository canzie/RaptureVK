#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLSL="$SCRIPT_DIR/shaders/glsl"
SPIRV="$SCRIPT_DIR/shaders/SPIRV"

mkdir -p "$SPIRV/Generators" "$SPIRV/shadows"

FAILED=0
compile() {
    local stage="$1" src="$2" out="$3"
    shift 3
    echo "  $src -> $out"
    if ! glslc -fshader-stage="$stage" "$@" "$GLSL/$src" -o "$SPIRV/$out"; then
        FAILED=$((FAILED + 1))
    fi
}

echo "Compiling shaders..."

compile vertex   default.vs.glsl                    default.vs.spv
compile fragment default.fs.glsl                    default.fs.spv

compile vertex   PBR.vs.glsl                        pbr.vs.spv
compile fragment PBR.fs.glsl                        pbr.fs.spv

compile vertex   GBuffer.vs.glsl                    GBuffer.vs.spv
compile fragment GBuffer.fs.glsl                    GBuffer.fs.spv           -I"$GLSL"

compile vertex   DeferredLighting.vs.glsl           DeferredLighting.vs.spv
compile fragment DeferredLighting.fs.glsl           DeferredLighting.fs.spv  -I"$GLSL/ddgi"

compile vertex   Skybox.vs.glsl                     SkyboxPass.vs.spv
compile fragment Skybox.fs.glsl                     SkyboxPass.fs.spv

compile vertex   StencilBorder.vs.glsl              StencilBorder.vs.spv
compile fragment StencilBorder.fs.glsl              StencilBorder.fs.spv

compile vertex   InstancedShapes.vs.glsl            InstancedShapes.vs.spv
compile fragment InstancedShapes.fs.glsl            InstancedShapes.fs.spv

compile vertex   Shadows/ShadowPass.vs.glsl         shadows/ShadowPass.vs.spv
compile vertex   Shadows/CascadedShadowPass.vs.glsl shadows/CascadedShadowPass.vs.spv

compile compute  Generators/PerlinNoise.cs.glsl     Generators/PerlinNoise.cs.spv

if [ "$FAILED" -gt 0 ]; then
    echo "FAILED: $FAILED shader(s) failed to compile"
    exit 1
fi

echo "All shaders compiled successfully"
