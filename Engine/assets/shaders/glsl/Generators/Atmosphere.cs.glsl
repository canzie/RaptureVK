#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba16f) uniform writeonly image2D outputTexture;

layout(push_constant) uniform PushConstants {
    vec3 sunDir;         // Normalized
    float planetRadius;  // e.g. 6371e3
    float atmoRadius;    // e.g. 6471e3
    vec3 betaRay;        // Rayleigh scattering: vec3(5.5e-6, 13.0e-6, 22.4e-6)
    float scaleHeight;   // e.g. 8000.0
    float sunIntensity;  // e.g. 20.0
} pc;

const float PI = 3.14159265359;
const int NUM_SAMPLES = 16;
const int NUM_SAMPLES_LIGHT = 8;

float getDensity(vec3 pos) {
    float height = length(pos) - pc.planetRadius;
    return exp(-height / pc.scaleHeight);
}

float opticalDepth(vec3 rayOrigin, vec3 rayDir, float rayLength) {
    float totalDepth = 0.0;
    int samples = 8;
    float stepSize = rayLength / float(samples);
    for(int i = 0; i < samples; i++) {
        vec3 p = rayOrigin + rayDir * (float(i) + 0.5) * stepSize;
        totalDepth += getDensity(p) * stepSize;
    }
    return totalDepth;
}

vec2 raySphereIntersect(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1.0, -1.0); // No intersection
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}


void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 res = imageSize(outputTexture);
    if (texel.x >= res.x || texel.y >= res.y) return;

    vec2 uv = (vec2(texel) / vec2(res)) * 2.0 - 1.0;
    uv.x *= float(res.x) / float(res.y); 
    vec3 rayDir = normalize(vec3(uv, 1.2)); 
    vec3 rayOrigin = vec3(0, pc.planetRadius + 1.0, 0);
    
    vec2 atmoDist = raySphereIntersect(rayOrigin, rayDir, pc.atmoRadius);
    vec2 planetDist = raySphereIntersect(rayOrigin, rayDir, pc.planetRadius);
    float t0 = max(atmoDist.x, 0.0);
    float t1 = (planetDist.x > 0.0) ? min(atmoDist.y, planetDist.x) : atmoDist.y;
    
    int samples = 16;
    float stepSize = (t1 - t0) / float(samples);
    vec3 scatterSum = vec3(0.0);
    float viewDepth = 0.0;

    for (int i = 0; i < samples; i++) {
        vec3 p = rayOrigin + rayDir * (t0 + (float(i) + 0.5) * stepSize);
        float dP = getDensity(p) * stepSize;
        viewDepth += dP;

        // Path to sun: calculate how much light reaches point 'p' from the sun
        vec2 sunAtmo = raySphereIntersect(p, pc.sunDir, pc.atmoRadius);
        float sunDepth = opticalDepth(p, pc.sunDir, sunAtmo.y);

        // Transmittance formula: T = e^(-beta * depth)
        // This accounts for light lost both on the way from the sun AND to the eye
        vec3 transmittance = exp(-pc.betaRay * (sunDepth + viewDepth));
        
        scatterSum += dP * transmittance;
    }

    float cosTheta = dot(rayDir, pc.sunDir);
    float phase = (3.0 / (16.0 * 3.14159)) * (1.0 + cosTheta * cosTheta);
    vec3 color = scatterSum * pc.betaRay * phase * pc.sunIntensity;

    imageStore(outputTexture, texel, vec4(color, 1.0));


}
