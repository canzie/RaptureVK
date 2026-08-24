#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vEdgeDistance;
layout(location = 2) in float vHalfThickness;

layout(location = 0) out vec4 outColor;

void main() {
    // Distance from the line's own edge, in pixels, since the quad was widened past it
    float coverage = clamp(vHalfThickness - abs(vEdgeDistance) + 0.5, 0.0, 1.0);

    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
