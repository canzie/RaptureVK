#version 450

layout(location = 0) in vec4 vColor;

#ifdef USE_SHADED_MODE
layout(location = 1) in vec3 vViewPosition;
#endif // USE_SHADED_MODE

layout(location = 0) out vec4 outColor;

#ifdef USE_SHADED_MODE
const float AMBIENT = 0.35;
#endif // USE_SHADED_MODE

void main() {
#ifdef USE_SHADED_MODE
    vec3 normal = normalize(cross(dFdx(vViewPosition), dFdy(vViewPosition)));

    // the light sits at the eye, so a shape reads the same from wherever it is looked at, and the
    // facing is taken unsigned since these shapes are drawn with their back faces
    float facing = abs(dot(normal, normalize(-vViewPosition)));

    outColor = vec4(vColor.rgb * (AMBIENT + (1.0 - AMBIENT) * facing), vColor.a);
#else
    outColor = vColor;
#endif // USE_SHADED_MODE
}
