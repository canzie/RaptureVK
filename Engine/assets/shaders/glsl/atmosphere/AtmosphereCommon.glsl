// AtmosphereCommon.glsl - the participating medium of a planet's atmosphere and the lookup tables built from it
// After Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
// The C++ AtmosphereUniforms struct in renderer/atmosphere/SkyAtmosphere.h must match the uniform block exactly.
//
// Distances are kilometres, positions are in the planet frame of common/PlanetFrame.glsl.
//
// The includer declares `layout(set = 3, binding = 0) uniform sampler2D gTextures[];` before including this.

#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

#include "common/LookupTable.glsl"
#include "common/ParticipatingMedia.glsl"
#include "common/PlanetFrame.glsl"

layout(std140, set = 4, binding = 1) uniform AtmosphereUniformBlock {
    vec4 rayleighScattering;  // xyz per km, w = rayleigh density exponent scale (-1 / scale height)
    vec4 mieScattering;       // xyz per km, w = mie density exponent scale
    vec4 mieExtinction;       // xyz per km, w = mie phase asymmetry g
    vec4 ozoneAbsorption;     // xyz per km, w = unused
    vec4 groundAlbedo;        // xyz, w = ground radius in km
    vec4 sunDirection;        // xyz towards the sun, w = atmosphere thickness in km
    vec4 sunIlluminance;      // xyz at the top of the atmosphere, w = cosine of the sun disk's angular radius
    uint transmittanceLut;
    uint multipleScatteringLut;
    float originAltitude;     // km above the ground of the world origin
    float unused;
} u_atmosphere;

const uint TRANSMITTANCE_LUT_WIDTH = 256;
const uint TRANSMITTANCE_LUT_HEIGHT = 64;
const uint MULTIPLE_SCATTERING_LUT_SIZE = 32;
const uint SKY_VIEW_LUT_WIDTH = 192;
const uint SKY_VIEW_LUT_HEIGHT = 108;
const uint AERIAL_PERSPECTIVE_SIZE = 32;
const float AERIAL_PERSPECTIVE_KM_PER_SLICE = 1.0;

// The ozone layer is a tent 30 km wide centred at 25 km, as in Bruneton 2017
const float OZONE_CENTER_KM = 25.0;
const float OZONE_HALF_WIDTH_KM = 15.0;

// Keeps sample points off the ground sphere, where the ground intersection would find the point itself
const float PLANET_RADIUS_OFFSET_KM = 0.01;

float atmosphereGroundRadius() {
    return u_atmosphere.groundAlbedo.w;
}

float atmosphereThickness() {
    return u_atmosphere.sunDirection.w;
}

struct AtmosphereMedium {
    vec3 scattering;
    vec3 extinction;
    vec3 rayleighScattering;
    vec3 mieScattering;
};

AtmosphereMedium atmosphereSampleMedium(float _altitude) {
    float rayleighDensity = exp(u_atmosphere.rayleighScattering.w * _altitude);
    float mieDensity = exp(u_atmosphere.mieScattering.w * _altitude);
    float ozoneDensity = max(0.0, 1.0 - abs(_altitude - OZONE_CENTER_KM) / OZONE_HALF_WIDTH_KM);

    AtmosphereMedium medium;
    medium.rayleighScattering = rayleighDensity * u_atmosphere.rayleighScattering.xyz;
    medium.mieScattering = mieDensity * u_atmosphere.mieScattering.xyz;
    medium.scattering = medium.rayleighScattering + medium.mieScattering;
    medium.extinction = medium.rayleighScattering + mieDensity * u_atmosphere.mieExtinction.xyz +
                        ozoneDensity * u_atmosphere.ozoneAbsorption.xyz;
    return medium;
}

// Bruneton and Neyret 2008: the distance to the top of the atmosphere, remapped between its minimum and maximum
// for the altitude, parameterises the view zenith angle. Rays below the horizon are not stored.
vec2 atmosphereTransmittanceUv(float _altitude, float _cosZenith) {
    float R = atmosphereGroundRadius();
    float top = atmosphereThickness();
    float H = sqrt(top * (2.0 * R + top));
    float rho = sqrt(max(_altitude * (2.0 * R + _altitude), 0.0));
    float r = R + _altitude;

    float discriminant = H * H - rho * rho + r * r * _cosZenith * _cosZenith;
    float d = max(0.0, -r * _cosZenith + sqrt(max(discriminant, 0.0)));
    float dMin = top - _altitude;
    float dMax = rho + H;

    return vec2((d - dMin) / (dMax - dMin), rho / H);
}

void atmosphereTransmittanceParams(vec2 _uv, out float _altitude, out float _cosZenith) {
    float R = atmosphereGroundRadius();
    float top = atmosphereThickness();
    float H = sqrt(top * (2.0 * R + top));
    float rho = H * _uv.y;
    _altitude = rho * rho / (sqrt(rho * rho + R * R) + R);
    float r = R + _altitude;

    float dMin = top - _altitude;
    float dMax = rho + H;
    float d = dMin + _uv.x * (dMax - dMin);
    _cosZenith = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    _cosZenith = clamp(_cosZenith, -1.0, 1.0);
}

vec3 atmosphereSampleTransmittance(float _altitude, float _cosZenith) {
    vec2 uv = atmosphereTransmittanceUv(_altitude, _cosZenith);
    return textureLod(gTextures[nonuniformEXT(u_atmosphere.transmittanceLut)], uv, 0.0).rgb;
}

// Psi_ms: the light that reaches a point after two or more bounces, per unit of sun illuminance
vec3 atmosphereSampleMultipleScattering(float _altitude, float _cosSunZenith) {
    vec2 uv = clamp(vec2(_cosSunZenith * 0.5 + 0.5, _altitude / atmosphereThickness()), 0.0, 1.0);
    float size = float(MULTIPLE_SCATTERING_LUT_SIZE);
    uv = vec2(lookupTableUnitToSubUv(uv.x, size), lookupTableUnitToSubUv(uv.y, size));
    return textureLod(gTextures[nonuniformEXT(u_atmosphere.multipleScatteringLut)], uv, 0.0).rgb;
}

// The sky-view lookup table is a latitude/longitude map around the viewer's up vector. Latitude is squeezed
// towards the horizon, where the sky changes fastest. Longitude is the azimuth from the sun, the sky being
// symmetric around the sun's vertical plane, squeezed towards the sun.
vec2 atmosphereSkyViewUv(float _altitude, float _cosZenith, float _cosLightView) {
    float horizonCos = -planetHorizonCosZenith(_altitude, atmosphereGroundRadius());
    float beta = acos(clamp(horizonCos, -1.0, 1.0));
    float zenithHorizonAngle = MEDIA_PI - beta;
    float zenithAngle = acos(clamp(_cosZenith, -1.0, 1.0));

    vec2 uv;
    if (zenithAngle < zenithHorizonAngle) {
        float coord = 1.0 - zenithAngle / zenithHorizonAngle;
        coord = 1.0 - sqrt(max(coord, 0.0));
        uv.y = coord * 0.5;
    } else {
        float coord = (zenithAngle - zenithHorizonAngle) / max(beta, 1e-6);
        uv.y = sqrt(clamp(coord, 0.0, 1.0)) * 0.5 + 0.5;
    }

    uv.x = sqrt(clamp(-_cosLightView * 0.5 + 0.5, 0.0, 1.0));

    return vec2(lookupTableUnitToSubUv(uv.x, float(SKY_VIEW_LUT_WIDTH)), lookupTableUnitToSubUv(uv.y, float(SKY_VIEW_LUT_HEIGHT)));
}

void atmosphereSkyViewParams(vec2 _uv, float _altitude, out float _cosZenith, out float _cosLightView) {
    _uv = vec2(lookupTableSubUvToUnit(_uv.x, float(SKY_VIEW_LUT_WIDTH)), lookupTableSubUvToUnit(_uv.y, float(SKY_VIEW_LUT_HEIGHT)));

    float horizonCos = -planetHorizonCosZenith(_altitude, atmosphereGroundRadius());
    float beta = acos(clamp(horizonCos, -1.0, 1.0));
    float zenithHorizonAngle = MEDIA_PI - beta;

    if (_uv.y < 0.5) {
        float coord = 1.0 - 2.0 * _uv.y;
        coord = 1.0 - coord * coord;
        _cosZenith = cos(zenithHorizonAngle * coord);
    } else {
        float coord = _uv.y * 2.0 - 1.0;
        _cosZenith = cos(zenithHorizonAngle + beta * coord * coord);
    }

    float coord = _uv.x * _uv.x;
    _cosLightView = -(coord * 2.0 - 1.0);
}

// The cosine of the azimuth between a direction and the sun, around the up vector
float atmosphereCosLightView(vec3 _dir, vec3 _sunDir) {
    vec2 dirAzimuth = _dir.xz;
    vec2 sunAzimuth = _sunDir.xz;
    float lengths = length(dirAzimuth) * length(sunAzimuth);
    if (lengths < 1e-6) {
        return 1.0;
    }
    return clamp(dot(dirAzimuth, sunAzimuth) / lengths, -1.0, 1.0);
}

struct AtmosphereRaymarchResult {
    vec3 luminance;          // per unit of sun illuminance
    vec3 transmittance;
    vec3 multipleScatteringTransfer; // f_ms, only meaningful for the multiple scattering lookup table
};

const uint ATMOSPHERE_MARCH_PHASE = 1u;            // Rayleigh and Mie phase functions, otherwise isotropic
const uint ATMOSPHERE_MARCH_MULTIPLE_SCATTERING = 2u; // adds Psi_ms from the lookup table
const uint ATMOSPHERE_MARCH_GROUND = 4u;           // adds the sunlit ground the ray ends on
const uint ATMOSPHERE_MARCH_VARIABLE_STEPS = 8u;   // more steps on longer rays, spaced quadratically

// Integrates single scattering of a unit sun along a ray (Equations 1 to 4, and 11 with multiple scattering),
// from _local over at most _maxDistance. _steps is the step count, or the maximum with variable steps.
AtmosphereRaymarchResult atmosphereRaymarch(vec3 _local, vec3 _dir, vec3 _sunDir, float _steps, float _maxDistance, uint _flags) {
    AtmosphereRaymarchResult result;
    result.luminance = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.multipleScatteringTransfer = vec3(0.0);

    float top = atmosphereThickness();
    vec2 topHits = planetIntersectShell(_local, _dir, atmosphereGroundRadius(), top);
    if (topHits.y < 0.0) {
        return result;
    }

    float tStart = max(topHits.x, 0.0);
    float tEnd = topHits.y;
    float tGround = planetIntersectShellNearest(_local, _dir, atmosphereGroundRadius(), 0.0);
    bool endsOnGround = tGround >= 0.0 && tGround < tEnd;
    if (endsOnGround) {
        tEnd = tGround;
    }
    if (tEnd > _maxDistance) {
        tEnd = _maxDistance;
        endsOnGround = false;
    }
    if (tEnd <= tStart) {
        return result;
    }

    float rayLength = tEnd - tStart;
    float steps = _steps;
    if ((_flags & ATMOSPHERE_MARCH_VARIABLE_STEPS) != 0u) {
        steps = max(4.0, floor(mix(4.0, _steps, clamp(rayLength * 0.01, 0.0, 1.0))));
    }

    float cosTheta = dot(_sunDir, _dir);
    float miePhase = mediaCornetteShanksPhase(u_atmosphere.mieExtinction.w, cosTheta);
    float rayleighPhase = mediaRayleighPhase(cosTheta);
    float uniformPhase = mediaUniformPhase();

    // The sample sits 30% into each segment, as in the reference implementation
    const float SAMPLE_OFFSET = 0.3;

    vec3 throughput = vec3(1.0);
    for (float s = 0.0; s < steps; s += 1.0) {
        float t0 = s / steps;
        float t1 = (s + 1.0) / steps;
        if ((_flags & ATMOSPHERE_MARCH_VARIABLE_STEPS) != 0u) {
            t0 = t0 * t0;
            t1 = t1 * t1;
        }
        t0 *= rayLength;
        t1 *= rayLength;
        float dt = t1 - t0;
        float t = t0 + dt * SAMPLE_OFFSET;

        vec3 samplePos = _local + (tStart + t) * _dir;
        float altitude = max(planetAltitude(samplePos, atmosphereGroundRadius()), 0.0);
        AtmosphereMedium medium = atmosphereSampleMedium(altitude);
        vec3 sampleTransmittance = mediaTransmittance(medium.extinction, dt);

        vec3 up = planetUp(samplePos, atmosphereGroundRadius());
        float cosSunZenith = dot(_sunDir, up);
        vec3 transmittanceToSun = atmosphereSampleTransmittance(altitude, cosSunZenith);

        // Lifting the start point keeps a sample on the ground from shadowing itself
        float tPlanet = planetIntersectShellNearest(samplePos + up * PLANET_RADIUS_OFFSET_KM, _sunDir, atmosphereGroundRadius(), 0.0);
        float planetShadow = tPlanet >= 0.0 ? 0.0 : 1.0;

        vec3 phaseTimesScattering;
        if ((_flags & ATMOSPHERE_MARCH_PHASE) != 0u) {
            phaseTimesScattering = medium.mieScattering * miePhase + medium.rayleighScattering * rayleighPhase;
        } else {
            phaseTimesScattering = medium.scattering * uniformPhase;
        }

        vec3 multipleScattering = vec3(0.0);
        if ((_flags & ATMOSPHERE_MARCH_MULTIPLE_SCATTERING) != 0u) {
            multipleScattering = atmosphereSampleMultipleScattering(altitude, cosSunZenith);
        }

        vec3 inScattered = planetShadow * transmittanceToSun * phaseTimesScattering + multipleScattering * medium.scattering;

        vec3 stepIntegral = mediaStepIntegral(medium.extinction, sampleTransmittance);

        result.luminance += throughput * inScattered * stepIntegral;
        result.multipleScatteringTransfer += throughput * medium.scattering * stepIntegral;
        throughput *= sampleTransmittance;
    }

    if ((_flags & ATMOSPHERE_MARCH_GROUND) != 0u && endsOnGround) {
        vec3 groundPos = _local + tEnd * _dir;
        vec3 up = planetUp(groundPos, atmosphereGroundRadius());
        float cosSunZenith = dot(_sunDir, up);
        vec3 transmittanceToSun = atmosphereSampleTransmittance(0.0, cosSunZenith);
        float cosIncidence = clamp(cosSunZenith, 0.0, 1.0);
        result.luminance += transmittanceToSun * throughput * cosIncidence * u_atmosphere.groundAlbedo.rgb / MEDIA_PI;
    }

    result.transmittance = throughput;
    return result;
}

// The local position of a world position, given in metres
vec3 atmosphereLocalFromWorld(vec3 _worldPos) {
    vec3 local = _worldPos * 0.001;
    local.y += u_atmosphere.originAltitude;
    local.y = max(local.y, PLANET_RADIUS_OFFSET_KM * 0.1);
    return local;
}

#endif // ATMOSPHERE_COMMON_GLSL
