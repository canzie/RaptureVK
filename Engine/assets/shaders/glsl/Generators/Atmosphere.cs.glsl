// Atmospheric Scattering - Compute Shader Implementation
// Based on: https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering
// Original paper: Nishita et al. 1993 "Display of the Earth Taking into Account Atmospheric Scattering"

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#ifdef OUTPUT_CUBEMAP
layout(set = 4, binding = 0, rgba16f) uniform writeonly image2DArray outputTexture;
#else
layout(set = 4, binding = 0, rgba16f) uniform writeonly image2D outputTexture;
#endif

// Annotations: @hidden, @range(min,max), @default(val), @name("Display Name"), @color
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;       // @hidden @default(0.0, 1.0003, 0.0)
    float innerRadius;    // @hidden @default(1.0)

    vec3 sunDirection;    // @name("Sun Direction") @range(-1.0, 1.0) @default(0.0, 0.3, -0.95)
    float outerRadius;    // @hidden @default(1.025)

    vec3 cameraDir;       // @name("Camera Direction") @range(-1.0, 1.0) @default(0.0, 0.0, -1.0)
    float scaleDepth;     // @hidden @default(0.25)

    vec3 cameraUp;        // @name("Camera Up") @range(-1.0, 1.0) @default(0.0, 1.0, 0.0)
    float kr;             // @hidden @default(0.0025)

    vec3 invWavelength;   // @name("Wavelength Scatter (RGB)") @range(0.0, 50.0) @default(5.8, 13.5, 33.1)
    float km;             // @name("Mie (Km)") @range(0.0, 60.0) @default(21.0)

    float eSun;           // @name("Sun Intensity") @range(1.0, 40.0) @default(20.0)
    float g;              // @name("Mie Phase (g)") @range(0.7, 0.999) @default(0.76)
    float fovY;           // @name("Field of View") @range(0.5, 3.0) @default(1.5708)
    float cameraAltitude; // @name("Camera Altitude") @range(0.0, 5000.0) @default(1.0)
} pc;

const float PI = 3.14159265359;
const int NUM_SAMPLES = 16;
const int NUM_LIGHT_SAMPLES = 8;

// Ray-sphere intersection - returns distance to intersection points
// Returns vec2(near, far), or vec2(-1) if no intersection
vec2 s_raySphereIntersect(vec3 rayOrigin, vec3 rayDir, float radius) {
    float b = dot(rayOrigin, rayDir);
    float c = dot(rayOrigin, rayOrigin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return vec2(-1.0);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

// Earth atmosphere constants (meters)
const float Rg = 6360.0e3; // ground / planet radius
const float Ra = 6420.0e3; // top of atmosphere
const float Hr = 7994.0;   // Rayleigh scale height
const float Hm = 1200.0;   // Mie scale height

// Single-scattering integration (Nishita 1993 / scratchapixel)
vec3 s_scatter(vec3 origin, vec3 dir, vec3 sunDir, vec3 betaR, vec3 betaM, float g, float sunIntensity) {
    vec2 atmoHit = s_raySphereIntersect(origin, dir, Ra);
    if (atmoHit.y < 0.0) return vec3(0.0);

    float tMin = max(atmoHit.x, 0.0);
    float tMax = atmoHit.y;

    // Clip the view ray to the ground so we don't integrate through the planet
    vec2 groundHit = s_raySphereIntersect(origin, dir, Rg);
    if (groundHit.x > 0.0) tMax = min(tMax, groundHit.x);

    float segment = (tMax - tMin) / float(NUM_SAMPLES);

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;

    float mu = dot(dir, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g2 = g * g;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
                   ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));

    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec3 samplePos = origin + dir * (tMin + (float(i) + 0.5) * segment);
        float height = length(samplePos) - Rg;
        float hr = exp(-height / Hr) * segment;
        float hm = exp(-height / Hm) * segment;
        opticalDepthR += hr;
        opticalDepthM += hm;

        // March from this sample toward the sun
        vec2 lightHit = s_raySphereIntersect(samplePos, sunDir, Ra);
        float segmentLight = lightHit.y / float(NUM_LIGHT_SAMPLES);
        float opticalDepthLightR = 0.0;
        float opticalDepthLightM = 0.0;
        int j;
        for (j = 0; j < NUM_LIGHT_SAMPLES; j++) {
            vec3 samplePosLight = samplePos + sunDir * (float(j) + 0.5) * segmentLight;
            float heightLight = length(samplePosLight) - Rg;
            if (heightLight < 0.0) break; // sun blocked by the planet
            opticalDepthLightR += exp(-heightLight / Hr) * segmentLight;
            opticalDepthLightM += exp(-heightLight / Hm) * segmentLight;
        }

        // Only fully lit samples contribute (soft terminator instead of a hard cutoff)
        if (j == NUM_LIGHT_SAMPLES) {
            vec3 tau = betaR * (opticalDepthR + opticalDepthLightR) +
                       betaM * 1.1 * (opticalDepthM + opticalDepthLightM);
            vec3 attenuation = exp(-tau);
            sumR += attenuation * hr;
            sumM += attenuation * hm;
        }
    }

    return sunIntensity * (sumR * betaR * phaseR + sumM * betaM * phaseM);
}

// Convert pixel to view direction
vec3 s_getViewDir(ivec2 texel, ivec2 res, vec3 forward, vec3 up, float fov, float aspect) {
    // UV: x from -1 (left) to +1 (right), y from -1 (bottom) to +1 (top)
    // Note: flip Y because image coords have Y=0 at top
    vec2 uv;
    uv.x = (float(texel.x) + 0.5) / float(res.x) * 2.0 - 1.0;
    uv.y = 1.0 - (float(texel.y) + 0.5) / float(res.y) * 2.0;  // Flipped!
    
    vec3 right = normalize(cross(forward, up));
    vec3 trueUp = cross(right, forward);
    
    float tanFov = tan(fov * 0.5);
    return normalize(forward + right * uv.x * tanFov * aspect + trueUp * uv.y * tanFov);
}

// Convert a cubemap face index plus in-face UV (-1..1) to a world-space direction
// Face order matches Vulkan cube layers: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
vec3 s_getCubeDir(int face, vec2 uv) {
    vec3 dir;
    if (face == 0)      dir = vec3( 1.0, -uv.y, -uv.x); // +X
    else if (face == 1) dir = vec3(-1.0, -uv.y,  uv.x); // -X
    else if (face == 2) dir = vec3( uv.x,  1.0,  uv.y); // +Y
    else if (face == 3) dir = vec3( uv.x, -1.0, -uv.y); // -Y
    else if (face == 4) dir = vec3( uv.x, -uv.y,  1.0); // +Z
    else                dir = vec3(-uv.x, -uv.y, -1.0); // -Z
    return normalize(dir);
}

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
#ifdef OUTPUT_CUBEMAP
    ivec2 res = imageSize(outputTexture).xy;
#else
    ivec2 res = imageSize(outputTexture);
#endif
    if (texel.x >= res.x || texel.y >= res.y) return;

#ifdef OUTPUT_CUBEMAP
    // Each layer is one cube face; cameraDir/cameraUp/fovY are unused (faces fix a 90 degree view)
    int face = int(gl_GlobalInvocationID.z);
    vec2 uv = (vec2(texel) + 0.5) / vec2(res) * 2.0 - 1.0;
    vec3 viewDir = s_getCubeDir(face, uv);
#else
    // Compute aspect ratio from texture dimensions
    float aspectRatio = float(res.x) / float(res.y);

    vec3 viewDir = s_getViewDir(
        texel, res,
        normalize(pc.cameraDir),
        normalize(pc.cameraUp),
        pc.fovY,
        aspectRatio
    );
#endif

    // betaR from the wavelength tint, betaM scalar; both in 1/m (the 1e-6 scale is the physical unit)
    vec3 betaR = pc.invWavelength * 1.0e-6;
    vec3 betaM = vec3(pc.km) * 1.0e-6;
    vec3 cameraPos = vec3(0.0, Rg + pc.cameraAltitude, 0.0);

    vec3 color = s_scatter(cameraPos, viewDir, normalize(pc.sunDirection), betaR, betaM, pc.g, pc.eSun);

#ifndef OUTPUT_CUBEMAP
    // The flat preview is displayed as-is, so it is the only path that tone maps. The cubemap is
    // radiance: the skybox pass, the DDGI miss ray and the IBL bake all consume it linearly, and the
    // composite pass tone maps once at the end.
    color = 1.0 - exp(-color);
#endif

#ifdef OUTPUT_CUBEMAP
    imageStore(outputTexture, ivec3(texel, face), vec4(color, 1.0));
#else
    imageStore(outputTexture, texel, vec4(color, 1.0));
#endif
}
