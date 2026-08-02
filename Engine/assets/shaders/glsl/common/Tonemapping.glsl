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

// GT7 Tone Mapping (Polyphony Digital, SIGGRAPH 2025) in its SDR configuration, using ICtCp as the
// uniform colour space. Reference implementation:
// https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp

// cd/m^2 that a frame buffer value of 1.0 represents
const float GT7_REFERENCE_LUMINANCE = 100.0;
// cd/m^2 that an SDR output value of 1.0 represents
const float GT7_SDR_PAPER_WHITE = 250.0;
// Peak the curve maps to, and the rescale that brings it back into the sRGB 0-1 range
const float GT7_LUMINANCE_TARGET = GT7_SDR_PAPER_WHITE / GT7_REFERENCE_LUMINANCE;
const float GT7_SDR_CORRECTION = 1.0 / GT7_LUMINANCE_TARGET;

const float GT7_ALPHA = 0.25;
const float GT7_MID_POINT = 0.538;
const float GT7_LINEAR_SECTION = 0.444;
const float GT7_TOE_STRENGTH = 1.280;

const float GT7_K = (GT7_LINEAR_SECTION - 1.0) / (GT7_ALPHA - 1.0);
const float GT7_KA = GT7_LUMINANCE_TARGET * GT7_LINEAR_SECTION + GT7_LUMINANCE_TARGET * GT7_K;
const float GT7_KB = -GT7_LUMINANCE_TARGET * GT7_K * exp(GT7_LINEAR_SECTION / GT7_K);
const float GT7_KC = -1.0 / (GT7_K * GT7_LUMINANCE_TARGET);

// Weight of the chroma preserving result over the per channel result
const float GT7_BLEND_RATIO = 0.6;
// Luminance range, normalised against the target, over which chroma fades out
const float GT7_FADE_START = 0.98;
const float GT7_FADE_END = 1.16;

const mat3 GT7_REC709_TO_REC2020 = mat3(
    0.627404, 0.069097, 0.016392,
    0.329282, 0.919541, 0.088013,
    0.043314, 0.011361, 0.895595);

const mat3 GT7_REC2020_TO_REC709 = mat3(
    1.660491, -0.124551, -0.018151,
    -0.587641, 1.132900, -0.100579,
    -0.072850, -0.008349, 1.118730);

// ST-2084 (PQ) EOTF, normalised signal to frame buffer linear
float GT7_eotfSt2084(float _n) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float pqC = 10000.0;

    _n = clamp(_n, 0.0, 1.0);

    float np = pow(_n, 1.0 / m2);
    float l = max(np - c1, 0.0);
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0 / m1);

    return (l * pqC) / GT7_REFERENCE_LUMINANCE;
}

// ST-2084 (PQ) inverse EOTF, frame buffer linear to normalised signal
float GT7_inverseEotfSt2084(float _v) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float pqC = 10000.0;

    float y = max((_v * GT7_REFERENCE_LUMINANCE) / pqC, 0.0);
    float ym = pow(y, m1);

    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0 + c3 * ym)));
}

vec3 GT7_rgbToICtCp(vec3 _rgb) {
    float l = dot(_rgb, vec3(1688.0, 2146.0, 262.0)) / 4096.0;
    float m = dot(_rgb, vec3(683.0, 2951.0, 462.0)) / 4096.0;
    float s = dot(_rgb, vec3(99.0, 309.0, 3688.0)) / 4096.0;

    float lPQ = GT7_inverseEotfSt2084(l);
    float mPQ = GT7_inverseEotfSt2084(m);
    float sPQ = GT7_inverseEotfSt2084(s);

    return vec3(
        (2048.0 * lPQ + 2048.0 * mPQ) / 4096.0,
        (6610.0 * lPQ - 13613.0 * mPQ + 7003.0 * sPQ) / 4096.0,
        (17933.0 * lPQ - 17390.0 * mPQ - 543.0 * sPQ) / 4096.0);
}

vec3 GT7_iCtCpToRgb(vec3 _ictCp) {
    float l = _ictCp.x + 0.00860904 * _ictCp.y + 0.11103 * _ictCp.z;
    float m = _ictCp.x - 0.00860904 * _ictCp.y - 0.11103 * _ictCp.z;
    float s = _ictCp.x + 0.560031 * _ictCp.y - 0.320627 * _ictCp.z;

    float lLin = GT7_eotfSt2084(l);
    float mLin = GT7_eotfSt2084(m);
    float sLin = GT7_eotfSt2084(s);

    return max(vec3(
        3.43661 * lLin - 2.50645 * mLin + 0.0698454 * sLin,
        -0.79133 * lLin + 1.9836 * mLin - 0.192271 * sLin,
        -0.0259499 * lLin - 0.0989137 * mLin + 1.12486 * sLin), vec3(0.0));
}

// GT tone mapping curve with a convergent shoulder
float GT7_evaluateCurve(float _x) {
    if (_x < 0.0) {
        return 0.0;
    }

    if (_x >= GT7_LINEAR_SECTION * GT7_LUMINANCE_TARGET) {
        return GT7_KA + GT7_KB * exp(_x * GT7_KC);
    }

    float weightLinear = smoothstep(0.0, GT7_MID_POINT, _x);
    float toeMapped = GT7_MID_POINT * pow(_x / GT7_MID_POINT, GT7_TOE_STRENGTH);

    return mix(toeMapped, _x, weightLinear);
}

// Takes and returns linear Rec.709
vec3 GT7ToneMapping(vec3 _color) {
    vec3 rgb = GT7_REC709_TO_REC2020 * _color;

    vec3 ucs = GT7_rgbToICtCp(rgb);

    vec3 skewedRgb = vec3(
        GT7_evaluateCurve(rgb.r),
        GT7_evaluateCurve(rgb.g),
        GT7_evaluateCurve(rgb.b));
    vec3 skewedUcs = GT7_rgbToICtCp(skewedRgb);

    // The ICtCp intensity of an achromatic value is just its PQ encoding, as the LMS rows sum to one
    float targetI = GT7_inverseEotfSt2084(GT7_LUMINANCE_TARGET);
    float chromaScale = 1.0 - smoothstep(GT7_FADE_START, GT7_FADE_END, ucs.x / targetI);

    vec3 scaledRgb = GT7_iCtCpToRgb(vec3(skewedUcs.x, ucs.yz * chromaScale));

    vec3 blended = mix(skewedRgb, scaledRgb, GT7_BLEND_RATIO);
    vec3 mapped = GT7_SDR_CORRECTION * min(blended, vec3(GT7_LUMINANCE_TARGET));

    return clamp(GT7_REC2020_TO_REC709 * mapped, vec3(0.0), vec3(1.0));
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

vec3 SRGBToLinear(vec3 _srgb) {
    _srgb = clamp(_srgb, 0.0, 1.0);
    return mix(
        pow3((_srgb + 0.055) / 1.055, 2.4),
        _srgb / 12.92,
        LessThan(_srgb, 0.04045));
}

#endif // TONEMAPPING_GLSL
