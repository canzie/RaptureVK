#version 450 core

layout(location = 0) out vec2 outTexCoord;



void main() {
    vec2 positions[4] = vec2[](
        vec2(-1.0, -1.0), // 0: Bottom-left
        vec2( 1.0, -1.0), // 1: Bottom-right
        vec2(-1.0,  1.0), // 2: Top-left
        vec2( 1.0,  1.0)  // 3: Top-right
    );

    vec2 texCoords[4] = vec2[](
        vec2(0.0, 0.0), // UV for BL
        vec2(1.0, 0.0), // UV for BR
        vec2(0.0, 1.0), // UV for TL
        vec2(1.0, 1.0)  // UV for TR
    );

    // Fullscreen quad using two triangles:
    // Triangle 1: positions[0], positions[1], positions[2] (BL, BR, TL)
    // Triangle 2: positions[2], positions[1], positions[3] (TL, BR, TR)

    vec2 finalPos;
    vec2 finalTexCoord;

    // Select vertices for the 6-vertex draw call
    if (gl_VertexIndex == 0) { finalPos = positions[0]; finalTexCoord = texCoords[0]; }      // T1 - V0 (BL)
    else if (gl_VertexIndex == 1) { finalPos = positions[1]; finalTexCoord = texCoords[1]; } // T1 - V1 (BR)
    else if (gl_VertexIndex == 2) { finalPos = positions[2]; finalTexCoord = texCoords[2]; } // T1 - V2 (TL)
    else if (gl_VertexIndex == 3) { finalPos = positions[2]; finalTexCoord = texCoords[2]; } // T2 - V0 (TL)
    else if (gl_VertexIndex == 4) { finalPos = positions[1]; finalTexCoord = texCoords[1]; } // T2 - V1 (BR)
    else if (gl_VertexIndex == 5) { finalPos = positions[3]; finalTexCoord = texCoords[3]; } // T2 - V2 (TR)
    else {
        // Default case, should not be reached with 6 vertices
        finalPos = vec2(0.0, 0.0);
        finalTexCoord = vec2(0.5, 0.5);
    }

    gl_Position = vec4(finalPos, 0.0, 1.0);
    outTexCoord = finalTexCoord;
}
