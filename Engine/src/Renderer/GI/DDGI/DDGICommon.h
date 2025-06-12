#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Rapture {


struct MeshInfo {
    alignas(4) uint32_t AlbedoTextureIndex;
    alignas(4) uint32_t NormalTextureIndex;
    alignas(4) uint32_t MetallicRoughnessTextureIndex;

};



struct SunProperties {
    alignas(16) glm::mat4 sunLightSpaceMatrix;    // Light-space matrices for each cascade
    alignas(16) glm::vec3 sunDirectionWorld;                       // Normalized direction FROM fragment TO sun
    alignas(16) glm::vec3 sunColor;

    alignas(4) float sunIntensity;
    alignas(4) uint32_t sunShadowTextureArrayIndex; // Descriptor array index for sampler2DShadow
};

struct ProbeVolume {
    alignas(16) glm::vec3 origin;

    alignas(16) glm::vec4 rotation;                           // rotation quaternion for the volume
    alignas(16) glm::vec4 probeRayRotation;                   // rotation quaternion for probe rays


    alignas(16) glm::vec3 spacing;
    alignas(16) glm::uvec3 gridDimensions;

    alignas(4) int      probeNumRays;                       // number of rays traced per probe
    alignas(4) int      probeNumIrradianceInteriorTexels;   // number of texels in one dimension of a probe's irradiance texture (does not include 1-texel border)
    alignas(4) int      probeNumDistanceInteriorTexels;     // number of texels in one dimension of a probe's distance texture (does not include 1-texel border)

    alignas(4) float    probeHysteresis;                    // weight of the previous irradiance and distance data store in probes
    alignas(4) float    probeMaxRayDistance;                // maximum world-space distance a probe ray can travel
    alignas(4) float    probeNormalBias;                    // offset along the surface normal, applied during lighting to avoid numerical instabilities when determining visibility
    alignas(4) float    probeViewBias;                      // offset along the camera view ray, applied during lighting to avoid numerical instabilities when determining visibility
    alignas(4) float    probeDistanceExponent;              // exponent used during visibility testing. High values react rapidly to depth discontinuities, but may cause banding
    alignas(4) float    probeIrradianceEncodingGamma;       // exponent that perceptually encodes irradiance for faster light-to-dark convergence

    alignas(4) float    probeBrightnessThreshold;

    // Probe Relocation, Probe Classification
    alignas(4) float    probeMinFrontfaceDistance;          // minimum world-space distance to a front facing triangle allowed before a probe is relocated

    alignas(4) float    probeRandomRayBackfaceThreshold;
    alignas(4) float    probeFixedRayBackfaceThreshold;
};

}


