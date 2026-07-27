#version 450 core

layout(location = 0) out vec2 outTexCoord;

// Fullscreen quad from a 6 vertex draw, no vertex buffer
void main() {
    vec2 positions[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0)
    );

    vec2 texCoords[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );

    int indices[6] = int[](0, 1, 2, 2, 1, 3);
    int index = indices[gl_VertexIndex];

    gl_Position = vec4(positions[index], 0.0, 1.0);
    outTexCoord = texCoords[index];
}
