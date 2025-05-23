#version 450

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform Material {
    vec3 albedo;
    float roughness;
} material;


void main() {

    outColor = vec4(material.albedo.x, material.roughness, material.albedo.z, 1.0);
}