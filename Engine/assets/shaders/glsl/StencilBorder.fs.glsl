#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vPosition;

layout(set = 3, binding = 0) uniform usampler2D u_gTextures[];

layout(push_constant) uniform PushConstants {
    uint modelDataIndex;
    vec4 color;
    float borderWidth;
    uint depthStencilTextureHandle;
    uint cameraUBOIndex;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Get current fragment coordinates
    vec2 texCoord = gl_FragCoord.xy / textureSize(u_gTextures[pc.depthStencilTextureHandle], 0);
    
    // Sample the stencil texture - note that stencil values are stored as unsigned integers
    uint stencilValue = texture(u_gTextures[pc.depthStencilTextureHandle], texCoord).r;
    
    // If the stencil value is 0, this means we're at the border
    // (assuming the main object is rendered with stencil value 1)
    if (stencilValue == 0u) {
        outColor = pc.color;
    } else {
        discard;
    }
}
