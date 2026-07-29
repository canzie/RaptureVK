#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common/CameraCommon.glsl"
#include "common/Octahedral.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform sampler2D gTextures[];

layout(set = 4, binding = 0, rgba16f) uniform restrict writeonly image2D outAccumulated;

layout(push_constant) uniform PushConstants {
    uint cameraSSBOIndex;
    uint cameraSlotIndex;
    uint resolvedTextureIndex;
    uint previousAccumulatedTextureIndex;
    uint depthTextureIndex;
    uint normalTextureIndex;
    uint hasHistory;
    float hysteresis;
    // Last, so the block ends on the 8 byte alignment the ivec2 gives it and its size matches
    // sizeof on the matching struct
    ivec2 outputSize;
} pc;

// How many standard deviations of its own neighbourhood the history is allowed to sit from the mean.
// Raising it keeps more history and so less noise, at the cost of letting more of it smear.
const float CLAMP_DEVIATIONS = 1.0;

void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.outputSize))) {
        return;
    }

    vec4 current = texelFetch(gTextures[nonuniformEXT(pc.resolvedTextureIndex)], dst, 0);
    bool currentValid = current.a >= 0.0;

    float depth = texelFetch(gTextures[nonuniformEXT(pc.depthTextureIndex)], dst, 0).r;
    if (depth >= 1.0) {
        imageStore(outAccumulated, dst, vec4(0.0));
        return;
    }

    CameraGPUData cam = u_cameraSSBO[pc.cameraSSBOIndex].cameras[pc.cameraSlotIndex];

    vec2 uv = (vec2(dst) + 0.5) / vec2(pc.outputSize);
    vec4 worldH = cam.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPos = worldH.xyz / worldH.w;

    vec3 worldNormal = octDecodeNormal(texelFetch(gTextures[nonuniformEXT(pc.normalTextureIndex)], dst, 0).rg);

    // A reflection does not travel with the surface showing it, it travels with parallax. Following
    // the surface's own motion vector smears every reflection across the geometry carrying it, so
    // the reprojection follows the virtual point the reflection appears to come from instead: the
    // hit pushed out along the reflected direction by the distance the resolve measured.
    vec3 viewPos = (cam.view * vec4(worldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(mat3(cam.view) * worldNormal);
    vec3 viewReflection = reflect(normalize(viewPos), viewNormal);

    // How far along that direction the virtual point sits. The resolve already averaged this over
    // the same rays and weights that made the radiance, so it moves with the surface rather than
    // with whichever direction this pixel happened to draw on this frame.
    float hitDistance = max(current.a, 0.0);

    // The view rotation is orthonormal, so its transpose undoes it without a matrix inverse
    vec3 worldReflection = transpose(mat3(cam.view)) * viewReflection;
    vec3 virtualPos = worldPos + worldReflection * hitDistance;

    // What is wanted is where this pixel's reflection sat last frame, not where the virtual point
    // itself was. Taking the virtual point's screen motion and stepping this pixel back along it
    // gives that, and it collapses to no movement at all when the camera is still.
    vec2 previousUV = uv - cameraMotionUV(cam, virtualPos);

    bool reprojected = pc.hasHistory != 0u && all(greaterThanEqual(previousUV, vec2(0.0))) &&
                       all(lessThanEqual(previousUV, vec2(1.0)));

    vec4 history = reprojected ? textureLod(gTextures[nonuniformEXT(pc.previousAccumulatedTextureIndex)], previousUV, 0.0)
                               : vec4(0.0);

    // Clamping the history into the range its own neighbourhood covers is what keeps a reprojection
    // that landed on unrelated geometry from dragging a stale reflection along with it. Only
    // neighbours that produced an estimate shape the bound, since one that found nothing describes
    // where the reflection is missing rather than how dark it is.
    //
    // The bound comes from the neighbourhood's mean and spread rather than its extremes. A single
    // stray sample sets the extremes for every pixel around it, so a min and max box lets each of
    // them keep history that bright and the stray grows into a blob instead of averaging out. It
    // moves the mean very little.
    vec3 sum = vec3(0.0);
    vec3 sumOfSquares = vec3(0.0);
    int boxSamples = 0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 coord = clamp(dst + ivec2(x, y), ivec2(0), pc.outputSize - 1);
            vec4 neighbour = texelFetch(gTextures[nonuniformEXT(pc.resolvedTextureIndex)], coord, 0);
            if (neighbour.a < 0.0) {
                continue;
            }
            sum += neighbour.rgb;
            sumOfSquares += neighbour.rgb * neighbour.rgb;
            ++boxSamples;
        }
    }

    // History is only worth keeping where the neighbourhood can still vouch for it. With nothing to
    // clamp against there is no evidence the accumulation describes this pixel any more, and holding
    // it anyway is what drags a reflection along behind the camera.
    float hysteresis = (reprojected && boxSamples > 0) ? pc.hysteresis : 0.0;

    float inverseCount = 1.0 / float(max(boxSamples, 1));
    vec3 mean = sum * inverseCount;
    vec3 deviation = sqrt(max(sumOfSquares * inverseCount - mean * mean, vec3(0.0)));
    vec3 clampedHistory = clamp(history.rgb, mean - deviation * CLAMP_DEVIATIONS, mean + deviation * CLAMP_DEVIATIONS);

    // A pixel whose own rays all missed contributes no radiance, so it decays toward zero rather
    // than holding its last value. Blending it against the clamped history instead would leave the
    // two sides of the mix identical and freeze the pixel wherever it last had a hit.
    vec3 radiance = mix(currentValid ? current.rgb : vec3(0.0), clampedHistory, hysteresis);

    // How much of this pixel is backed by rays that actually landed. A convex blend of zero and one,
    // so it stays bounded no matter how long the accumulation runs.
    float validity = mix(currentValid ? 1.0 : 0.0, history.a, hysteresis);

    imageStore(outAccumulated, dst, vec4(radiance, validity));
}
