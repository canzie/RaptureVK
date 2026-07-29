// ImportanceSampling.glsl - low discrepancy sequences and GGX lobe sampling

#ifndef IMPORTANCE_SAMPLING_GLSL
#define IMPORTANCE_SAMPLING_GLSL

#include "common/BRDF.glsl"

// A perfectly smooth GGX lobe is a delta function, which leaves both the sample warp and its
// density dividing by zero. Every consumer clamps to this so the sampled direction and the pdf the
// resolve divides by stay consistent.
const float MIN_GGX_ROUGHNESS = 0.02;

// How far the first sampling dimension is pulled toward the mirror direction before the lobe is
// sampled. The BRDF tail is where nearly all the variance lives and almost none of the energy, so
// giving it up is the cheapest noise reduction available.
//
// The truncated distribution has a different normalising constant than the pdf that gets stored, so
// the samples are no longer drawn from the density they are divided by. The resolve's weight sum
// normalisation divides that constant straight back out, which is what keeps this energy correct.
const float GGX_SAMPLING_BIAS = 0.7;

// The lobe the biased sampler actually covers is narrower than roughness alone implies, so a cone
// footprint built from it has to shrink to match or the reflection is filtered wider than sampled.
//
// The truncation scales the first dimension, and the sampler takes its square root to get a disk
// radius, so the radius shrinks by the root of the bias rather than the bias itself. Half vector
// slope follows that radius, so this is the scale the footprint has to match.
float biasedConeScale() {
    return sqrt(1.0 - GGX_SAMPLING_BIAS);
}

// Van der Corput radical inverse in base 2, the second dimension of a Hammersley sequence
float radicalInverseVdC(uint _bits) {
    _bits = (_bits << 16u) | (_bits >> 16u);
    _bits = ((_bits & 0x55555555u) << 1u) | ((_bits & 0xAAAAAAAAu) >> 1u);
    _bits = ((_bits & 0x33333333u) << 2u) | ((_bits & 0xCCCCCCCCu) >> 2u);
    _bits = ((_bits & 0x0F0F0F0Fu) << 4u) | ((_bits & 0xF0F0F0F0u) >> 4u);
    _bits = ((_bits & 0x00FF00FFu) << 8u) | ((_bits & 0xFF00FF00u) >> 8u);
    return float(_bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint _index, uint _count) {
    return vec2(float(_index) / float(_count), radicalInverseVdC(_index));
}

// A tangent frame with z along the normal
mat3 buildTangentFrame(vec3 _n) {
    vec3 up = abs(_n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, _n));
    vec3 bitangent = cross(_n, tangent);
    return mat3(tangent, bitangent, _n);
}

// Smith masking for a single direction, the G1 the visible normal distribution is defined against
float smithG1Ggx(float _NdotV, float _roughness) {
    float a = _roughness * _roughness;
    float a2 = a * a;
    return 2.0 * _NdotV / max(_NdotV + sqrt(a2 + (1.0 - a2) * _NdotV * _NdotV), 1e-5);
}

// Heitz 2018, sampling the GGX distribution of visible normals. _v is in the tangent frame, and the
// returned half vector is too. Sampling visible normals rather than the full NDF means every sample
// is one the viewer can actually see, which removes the wasted samples plain NDF sampling produces
// at grazing angles.
vec3 importanceSampleGgxVndf(vec2 _u, vec3 _v, float _roughness) {
    float alpha = _roughness * _roughness;

    // Stretch the view direction so the lobe becomes the roughness 1 hemisphere
    vec3 vh = normalize(vec3(alpha * _v.x, alpha * _v.y, _v.z));

    float lensq = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);

    // A uniform point on the disk, warped to the projected area of the visible hemisphere
    float r = sqrt(_u.x);
    float phi = 2.0 * PI * _u.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;

    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;

    // Unstretch back into the real lobe
    return normalize(vec3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
}

// Solid angle density of a direction produced by importanceSampleGgxVndf, after the half vector to
// light direction change of variables contributes its 1 / (4 VdotH) jacobian
float pdfGgxVndf(float _NdotH, float _NdotV, float _roughness) {
    return smithG1Ggx(_NdotV, _roughness) * D_GGX(_NdotH, _roughness) / max(4.0 * _NdotV, 1e-5);
}

// The specular lobe weight the resolve divides by its pdf: the BRDF times the cosine, with the
// Fresnel term left out because the split-sum FG carries it in at composite
float specularLobeWeight(float _NdotH, float _NdotV, float _NdotL, float _roughness) {
    return D_GGX(_NdotH, _roughness) * V_SmithGGXCorrelated(_NdotV, _NdotL, _roughness) * _NdotL;
}

#endif // IMPORTANCE_SAMPLING_GLSL
