#version 450 core

layout(location = 0) out vec2 outTexCoord;

void main() {
    // One triangle covering the screen, uv 0 to 1 across it
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    outTexCoord = uv;
}
