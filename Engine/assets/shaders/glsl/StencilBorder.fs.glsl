#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vPosition;

layout(set = 0, binding = 1) uniform usampler2D stencilTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    float borderWidth;
} pushConstants;

layout(location = 0) out vec4 outColor;

void main() {
    // Get current fragment coordinates
    vec2 texCoord = gl_FragCoord.xy / textureSize(stencilTexture, 0);
    
    // Sample the stencil texture - note that stencil values are stored as unsigned integers
    uint stencilValue = texture(stencilTexture, texCoord).r;
    
    // If the stencil value is 0, this means we're at the border
    // (assuming the main object is rendered with stencil value 1)
    if (stencilValue == 0u) {
        outColor = pushConstants.color;
    } else {
        discard;
    }
}
