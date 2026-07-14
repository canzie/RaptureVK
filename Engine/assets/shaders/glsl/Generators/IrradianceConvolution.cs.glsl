#version 450

// Diffuse irradiance convolution.
// Samples the source environment cube and integrates the cosine-weighted hemisphere
// around each output texel's direction, producing the diffuse irradiance cube.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 4, binding = 0) uniform samplerCube srcCube;
layout(set = 4, binding = 1, rgba16f) uniform writeonly image2DArray outputTexture;

const float PI = 3.14159265359;

// Cube face index + in-face UV (-1..1) to world direction.
// Face order matches Vulkan cube layers: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
vec3 s_getCubeDir(int face, vec2 uv) {
    vec3 dir;
    if (face == 0)      dir = vec3( 1.0, -uv.y, -uv.x);
    else if (face == 1) dir = vec3(-1.0, -uv.y,  uv.x);
    else if (face == 2) dir = vec3( uv.x,  1.0,  uv.y);
    else if (face == 3) dir = vec3( uv.x, -1.0, -uv.y);
    else if (face == 4) dir = vec3( uv.x, -uv.y,  1.0);
    else                dir = vec3(-uv.x, -uv.y, -1.0);
    return normalize(dir);
}

void main() {
    ivec3 texel = ivec3(gl_GlobalInvocationID);
    ivec2 res = imageSize(outputTexture).xy;
    if (texel.x >= res.x || texel.y >= res.y) {
        return;
    }

    vec2 uv = (vec2(texel.xy) + 0.5) / vec2(res) * 2.0 - 1.0;
    vec3 normal = s_getCubeDir(texel.z, uv);

    // Tangent frame around the normal
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    const float deltaPhi = 2.0 * PI / 180.0;
    const float deltaTheta = 0.5 * PI / 64.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += deltaPhi) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += deltaTheta) {
            // Spherical to cartesian in tangent space, then to world
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
            // cos(theta) weight for the projected solid angle, sin(theta) for the hemisphere measure
            irradiance += textureLod(srcCube, sampleDir, 0.0).rgb * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }

    irradiance = PI * irradiance / sampleCount;
    imageStore(outputTexture, texel, vec4(irradiance, 1.0));
}
