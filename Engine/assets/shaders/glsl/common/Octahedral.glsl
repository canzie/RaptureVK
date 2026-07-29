// Octahedral.glsl - octahedral mapping between unit vectors and a 2-component value

#ifndef OCTAHEDRAL_GLSL
#define OCTAHEDRAL_GLSL

vec2 signNotZero(vec2 _v) {
    return vec2(_v.x >= 0.0 ? 1.0 : -1.0, _v.y >= 0.0 ? 1.0 : -1.0);
}

// Octahedral encode a unit normal into a 2-component value in [-1, 1]
vec2 octEncodeNormal(vec3 _n) {
    _n /= (abs(_n.x) + abs(_n.y) + abs(_n.z));
    vec2 enc = (_n.z >= 0.0) ? _n.xy : (1.0 - abs(_n.yx)) * signNotZero(_n.xy);
    return enc;
}

// Decode an octahedral-encoded normal back to a unit vector
vec3 octDecodeNormal(vec2 _enc) {
    vec3 n = vec3(_enc.xy, 1.0 - abs(_enc.x) - abs(_enc.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * signNotZero(n.xy);
    }
    return normalize(n);
}

#endif // OCTAHEDRAL_GLSL
