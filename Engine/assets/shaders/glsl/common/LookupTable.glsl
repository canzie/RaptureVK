// LookupTable.glsl - mapping a [0, 1] parameter onto a lookup table's texels

#ifndef LOOKUP_TABLE_GLSL
#define LOOKUP_TABLE_GLSL

// Keeps a [0, 1] coordinate on texel centres, so the ends of the range land on the first and last texel exactly
float lookupTableUnitToSubUv(float _u, float _resolution) {
    return (_u + 0.5 / _resolution) * (_resolution / (_resolution + 1.0));
}

float lookupTableSubUvToUnit(float _u, float _resolution) {
    return (_u - 0.5 / _resolution) * (_resolution / (_resolution - 1.0));
}

#endif // LOOKUP_TABLE_GLSL
