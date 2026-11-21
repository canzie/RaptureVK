// PlanetFrame.glsl - positions around a spherical planet, relative to the ground point below the world origin
//
// Distances are kilometres, y up, so the planet centre sits at (0, -_groundRadius, 0). Squared radii are never
// formed directly: a few metres above a 6360 km sphere vanish in float precision once both are squared, and
// anything placed against the horizon shimmers.

#ifndef PLANET_FRAME_GLSL
#define PLANET_FRAME_GLSL

// |P|^2 - R^2 for a local position
float planetRadiusSquaredExcess(vec3 _local, float _groundRadius) {
    return _local.x * _local.x + _local.z * _local.z + _local.y * (2.0 * _groundRadius + _local.y);
}

float planetAltitude(vec3 _local, float _groundRadius) {
    float excess = planetRadiusSquaredExcess(_local, _groundRadius);
    float radius = sqrt(max(excess + _groundRadius * _groundRadius, 0.0));
    return excess / (radius + _groundRadius);
}

// The unit vector pointing away from the planet centre
vec3 planetUp(vec3 _local, float _groundRadius) {
    return normalize(_local + vec3(0.0, _groundRadius, 0.0));
}

// Distances along a ray to the sphere _shellAltitude above the ground, (-1, -1) on a miss
vec2 planetIntersectShell(vec3 _local, vec3 _dir, float _groundRadius, float _shellAltitude) {
    float b = dot(_local, _dir) + _groundRadius * _dir.y;
    float c = planetRadiusSquaredExcess(_local, _groundRadius) - _shellAltitude * (2.0 * _groundRadius + _shellAltitude);
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return vec2(-1.0);
    }
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

// Nearest positive hit with the shell, -1 when the ray misses it or it lies behind
float planetIntersectShellNearest(vec3 _local, vec3 _dir, float _groundRadius, float _shellAltitude) {
    vec2 hits = planetIntersectShell(_local, _dir, _groundRadius, _shellAltitude);
    if (hits.y < 0.0) {
        return -1.0;
    }
    return hits.x >= 0.0 ? hits.x : hits.y;
}

// The cosine of the zenith angle the horizon sits at for an altitude, rays below it hit the ground
float planetHorizonCosZenith(float _altitude, float _groundRadius) {
    float rho = sqrt(max(_altitude * (2.0 * _groundRadius + _altitude), 0.0));
    return -rho / (_groundRadius + _altitude);
}

#endif // PLANET_FRAME_GLSL
