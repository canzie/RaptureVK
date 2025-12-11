#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Rapture {

struct MeshInfo {
    alignas(4) uint32_t AlbedoTextureIndex;
    alignas(4) uint32_t NormalTextureIndex;

    alignas(16) glm::vec3 albedo;

    alignas(16) glm::vec3 emissiveColor;
    alignas(4) uint32_t EmissiveFactorTextureIndex;

    alignas(4) uint32_t iboIndex; // index of the buffer in the bindless buffers array
    alignas(4) uint32_t vboIndex; // index of the buffer in the bindless buffers array

    alignas(4)
        uint32_t meshIndex; // index of the mesh in the mesh array, this is the same index as the tlasinstance instanceCustomIndex

    alignas(16) glm::mat4 modelMatrix;

    alignas(4) uint32_t positionAttributeOffsetBytes; // Offset of position *within* the stride
    alignas(4) uint32_t texCoordAttributeOffsetBytes;
    alignas(4) uint32_t normalAttributeOffsetBytes;
    alignas(4) uint32_t tangentAttributeOffsetBytes;

    alignas(4) uint32_t vertexStrideBytes; // Stride of the vertex buffer in bytes
    alignas(4) uint32_t indexType;         // GL_UNSIGNED_INT (5125) or GL_UNSIGNED_SHORT (5123)
};

struct ProbeVolume {
    alignas(16) glm::vec3 origin;

    alignas(16) glm::vec4 rotation;         // rotation quaternion for the volume
    alignas(16) glm::vec4 probeRayRotation; // rotation quaternion for probe rays

    alignas(16) glm::vec3 spacing;
    alignas(16) glm::uvec3 gridDimensions;

    alignas(4) int probeNumRays; // number of rays traced per probe
    alignas(4) uint32_t probeStaticRayCount;

    alignas(4) int probeNumIrradianceTexels; // number of texels in one dimension of a probe's irradiance texture
    alignas(4) int probeNumDistanceTexels;   // number of texels in one dimension of a probe's distance texture

    alignas(4) int probeNumIrradianceInteriorTexels; // number of texels in one dimension of a probe's irradiance texture (does not
                                                     // include 1-texel border)
    alignas(4) int probeNumDistanceInteriorTexels;   // number of texels in one dimension of a probe's distance texture (does not
                                                     // include 1-texel border)

    alignas(4) float probeHysteresis;     // weight of the previous irradiance and distance data store in probes
    alignas(4) float probeMaxRayDistance; // maximum world-space distance a probe ray can travel
    alignas(4) float probeNormalBias; // offset along the surface normal, applied during lighting to avoid numerical instabilities
                                      // when determining visibility
    alignas(4) float probeViewBias;   // offset along the camera view ray, applied during lighting to avoid numerical instabilities
                                      // when determining visibility
    alignas(4) float probeDistanceExponent; // exponent used during visibility testing. High values react rapidly to depth
                                            // discontinuities, but may cause banding
    alignas(
        4) float probeIrradianceEncodingGamma; // exponent that perceptually encodes irradiance for faster light-to-dark convergence

    alignas(4) float probeBrightnessThreshold;

    // Probe Relocation, Probe Classification
    alignas(4) float probeMinFrontfaceDistance; // minimum world-space distance to a front facing triangle allowed before a probe is
                                                // relocated

    alignas(4) float probeRandomRayBackfaceThreshold;
    alignas(4) float probeFixedRayBackfaceThreshold;

    // Additional classification and relocation parameters
    alignas(4) float probeRelocationEnabled;     // enable/disable probe relocation (0.0 = disabled, 1.0 = enabled)
    alignas(4) float probeClassificationEnabled; // enable/disable probe classification (0.0 = disabled, 1.0 = enabled)
    alignas(4) float probeChangeThreshold;       // threshold for considering a probe's position has changed significantly
    alignas(4) float probeMinValidSamples;       // minimum number of valid ray samples required for a probe to be considered valid
};

} // namespace Rapture
