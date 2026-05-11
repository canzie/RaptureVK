// Atmospheric Scattering - Compute Shader Implementation
// Based on: https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering
// Original paper: Nishita et al. 1993 "Display of the Earth Taking into Account Atmospheric Scattering"

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0, rgba16f) uniform writeonly image2D outputTexture;

// Annotations: @hidden, @range(min,max), @default(val), @name("Display Name"), @color
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;       // @hidden @default(0.0, 1.0003, 0.0)
    float innerRadius;    // @hidden @default(1.0)

    vec3 sunDirection;    // @name("Sun Direction") @range(-1.0, 1.0) @default(0.5, 0.05, -0.8)
    float outerRadius;    // @name("Atmosphere Thickness") @range(1.02, 1.12) @default(1.06)

    vec3 cameraDir;       // @hidden @default(0.0, 0.0, -1.0)
    float scaleDepth;     // @name("Scale Depth") @range(0.15, 0.5) @default(0.25)

    vec3 cameraUp;        // @hidden @default(0.0, 1.0, 0.0)
    float kr;             // @name("Rayleigh (Kr)") @range(0.0015, 0.015) @default(0.0045)

    vec3 invWavelength;   // @name("Wavelength Scatter (RGB)") @range(0.0, 50.0) @default(5.8, 13.5, 33.1)
    float km;             // @name("Mie (Km)") @range(0.0, 0.015) @default(0.003)

    float eSun;           // @name("Sun Intensity") @range(1.0, 40.0) @default(18.0)
    float g;              // @name("Mie Phase (g)") @range(0.7, 0.999) @default(0.92)
    float fovY;           // @hidden @default(1.5708)
    float cameraAltitude; // @name("Camera Altitude") @range(0.0001, 0.02) @default(0.0003)
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

// Density at height (exponential falloff)
float s_density(float h, float H) {
    return exp(-max(h, 0.0) / H);
}

// Check if point is in planet's shadow (sun blocked by planet)
bool s_inShadow(vec3 p, vec3 sunDir, float innerR) {
    vec2 planetHit = s_raySphereIntersect(p, sunDir, innerR);
    // If the ray toward the sun hits the planet (positive intersection), we're in shadow
    return planetHit.x > 0.0 || planetHit.y > 0.0;
}

// Optical depth along ray from point to atmosphere edge
// Returns negative value if in shadow (sun blocked by planet)
float s_opticalDepth(vec3 p, vec3 dir, float H, float innerR, float outerR) {
    // Check if sun is blocked by planet from this point
    if (s_inShadow(p, dir, innerR)) {
        return -1.0; // Signal that this point receives no direct sunlight
    }
    
    vec2 t = s_raySphereIntersect(p, dir, outerR);
    if (t.y < 0.0) return 0.0;
    
    float rayLen = t.y;
    float stepSize = rayLen / float(NUM_LIGHT_SAMPLES);
    float depth = 0.0;
    
    for (int i = 0; i < NUM_LIGHT_SAMPLES; i++) {
        vec3 samplePos = p + dir * (float(i) + 0.5) * stepSize;
        float h = length(samplePos) - innerR;
        depth += s_density(h, H) * stepSize;
    }
    return depth;
}

// Rayleigh phase function
float s_phaseR(float cosTheta) {
    return 0.75 * (1.0 + cosTheta * cosTheta);
}

// Mie phase function (Henyey-Greenstein)
float s_phaseM(float cosTheta, float g) {
    float g2 = g * g;
    float num = 1.0 - g2;
    float denom = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return 1.5 * num / denom;
}

// Main scattering calculation
vec3 s_scatter(vec3 origin, vec3 dir, vec3 sunDir, float innerR, float outerR,
               float H, float Kr, float Km, float sunIntensity, float g, vec3 wavelengthInv) {
    
    // Find atmosphere intersection
    vec2 atmoHit = s_raySphereIntersect(origin, dir, outerR);
    if (atmoHit.y < 0.0) return vec3(0.0);
    
    // Check if we hit the planet
    vec2 planetHit = s_raySphereIntersect(origin, dir, innerR);
    
    float tMin = max(atmoHit.x, 0.0);
    float tMax = atmoHit.y;
    if (planetHit.x > 0.0) tMax = min(tMax, planetHit.x);
    
    if (tMax <= tMin) return vec3(0.0);
    
    // Scale height for this atmosphere
    float scaleH = H * (outerR - innerR);
    
    // Step through atmosphere
    float stepSize = (tMax - tMin) / float(NUM_SAMPLES);
    
    vec3 rayleighAccum = vec3(0.0);
    vec3 mieAccum = vec3(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec3 samplePos = origin + dir * (tMin + (float(i) + 0.5) * stepSize);
        float height = length(samplePos) - innerR;
        
        // Local density
        float densityR = s_density(height, scaleH);
        float densityM = densityR; // Same for Mie in simple model
        
        opticalDepthR += densityR * stepSize;
        opticalDepthM += densityM * stepSize;
        
        // Optical depth from sample to sun (negative means in shadow)
        float sunDepth = s_opticalDepth(samplePos, sunDir, scaleH, innerR, outerR);
        
        // Skip scattering contribution if this point is in planet's shadow
        if (sunDepth < 0.0) {
            continue;
        }
        
        // Total optical depth (to sun + to camera so far)
        vec3 tau = (opticalDepthR + sunDepth) * Kr * wavelengthInv +
                   (opticalDepthM + sunDepth) * Km;
        
        // Transmittance
        vec3 attenuation = exp(-tau);
        
        // Accumulate scattering
        rayleighAccum += densityR * attenuation * stepSize;
        mieAccum += densityM * attenuation * stepSize;
    }
    
    // Phase functions
    float cosTheta = dot(dir, sunDir);
    float phaseR = s_phaseR(cosTheta);
    float phaseM = s_phaseM(cosTheta, g);
    
    // Final color
    vec3 rayleigh = rayleighAccum * Kr * wavelengthInv * phaseR;
    vec3 mie = mieAccum * Km * phaseM;
    
    return sunIntensity * (rayleigh + mie);
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

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 res = imageSize(outputTexture);
    if (texel.x >= res.x || texel.y >= res.y) return;
    
    // Compute aspect ratio from texture dimensions
    float aspectRatio = float(res.x) / float(res.y);
    
    vec3 viewDir = s_getViewDir(
        texel, res,
        normalize(pc.cameraDir),
        normalize(pc.cameraUp),
        pc.fovY,
        aspectRatio
    );
    
    // Compute camera position from altitude (innerRadius + altitude)
    vec3 cameraPos = vec3(0.0, pc.innerRadius + pc.cameraAltitude, 0.0);
    
    vec3 color = s_scatter(
        cameraPos,
        viewDir,
        normalize(pc.sunDirection),
        pc.innerRadius,
        pc.outerRadius,
        pc.scaleDepth,
        pc.kr,
        pc.km,
        pc.eSun,
        pc.g,
        pc.invWavelength
    );
    
    // Tone mapping
    color = 1.0 - exp(-color);
    
    imageStore(outputTexture, texel, vec4(color, 1.0));
}
