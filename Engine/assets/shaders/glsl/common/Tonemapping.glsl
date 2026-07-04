// Tonemapping.glsl - exposure, tone mapping and color space helpers

#ifndef TONEMAPPING_GLSL
#define TONEMAPPING_GLSL

float exposure(float fstop) {
    return pow(2.0, fstop);
}

vec3 ACESFilm(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3(0.0), vec3(1.0));
}

vec3 LessThan(vec3 f, float value) {
    return vec3(
        (f.x < value) ? 1.0 : 0.0,
        (f.y < value) ? 1.0 : 0.0,
        (f.z < value) ? 1.0 : 0.0);
}

vec3 pow3(vec3 x, float y) {
    return vec3(pow(x.x, y), pow(x.y, y), pow(x.z, y));
}

vec3 LinearToSRGB(vec3 rgb) {
    rgb = clamp(rgb, 0.0, 1.0);
    return mix(
        pow3(rgb * 1.055, 1.0 / 2.4) - 0.055,
        rgb * 12.92,
        LessThan(rgb, 0.0031308));
}

#endif // TONEMAPPING_GLSL
