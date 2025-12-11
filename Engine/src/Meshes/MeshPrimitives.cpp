#include "MeshPrimitives.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Rapture {

Mesh Primitives::CreateCube()
{
    AllocatorParams params;

    // Setup buffer layout for cube (position, normal, UV)
    BufferLayout bufferLayout;
    BufferAttribute positionAttribute;
    positionAttribute.name = BufferAttributeID::POSITION;
    positionAttribute.componentType = FLOAT_TYPE;
    positionAttribute.type = "VEC3";
    positionAttribute.offset = 0;
    bufferLayout.buffer_attribs.push_back(positionAttribute);

    BufferAttribute normalAttribute;
    normalAttribute.name = BufferAttributeID::NORMAL;
    normalAttribute.componentType = FLOAT_TYPE;
    normalAttribute.type = "VEC3";
    normalAttribute.offset = 12;
    bufferLayout.buffer_attribs.push_back(normalAttribute);

    BufferAttribute uvAttribute;
    uvAttribute.name = BufferAttributeID::TEXCOORD_0;
    uvAttribute.componentType = FLOAT_TYPE;
    uvAttribute.type = "VEC2";
    uvAttribute.offset = 24;
    bufferLayout.buffer_attribs.push_back(uvAttribute);

    bufferLayout.calculateVertexSize();
    params.bufferLayout = bufferLayout;

    // Cube vertices (24 vertices, 4 per face for proper normals and UVs)
    // Format: position (3 floats), normal (3 floats), UV (2 floats)
    std::vector<float> vertices = {
        // Front face
        -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // Bottom-left
        0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,  // Bottom-right
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,   // Top-right
        -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // Top-left

        // Back face
        0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // Bottom-left
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, // Bottom-right
        -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,  // Top-right
        0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,   // Top-left

        // Left face
        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, // Bottom-left
        -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,  // Bottom-right
        -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,   // Top-right
        -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // Top-left

        // Right face
        0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,  // Bottom-left
        0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, // Bottom-right
        0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  // Top-right
        0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,   // Top-left

        // Top face
        -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,  // Bottom-left
        0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,   // Bottom-right
        0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,  // Top-right
        -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // Top-left

        // Bottom face
        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, // Bottom-left
        0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,  // Bottom-right
        0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,   // Top-right
        -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f   // Top-left
    };

    std::vector<uint16_t> indices = {// Front face
                                     0, 1, 2, 2, 3, 0,
                                     // Back face
                                     4, 5, 6, 6, 7, 4,
                                     // Left face
                                     8, 9, 10, 10, 11, 8,
                                     // Right face
                                     12, 13, 14, 14, 15, 12,
                                     // Top face
                                     16, 17, 18, 18, 19, 16,
                                     // Bottom face
                                     20, 21, 22, 22, 23, 20};

    params.vertexData = vertices.data();
    params.vertexDataSize = vertices.size() * sizeof(float);
    params.indexData = indices.data();
    params.indexDataSize = indices.size() * sizeof(uint16_t);
    params.indexCount = indices.size();
    params.indexType = UNSIGNED_SHORT_TYPE;

    return Mesh(params);
}

Mesh Primitives::CreateSphere(float radius, uint32_t segments)
{
    AllocatorParams params;

    // Setup buffer layout for sphere (position, normal, UV)
    BufferLayout bufferLayout;
    BufferAttribute positionAttribute;
    positionAttribute.name = BufferAttributeID::POSITION;
    positionAttribute.componentType = FLOAT_TYPE;
    positionAttribute.type = "VEC3";
    positionAttribute.offset = 0;
    bufferLayout.buffer_attribs.push_back(positionAttribute);

    BufferAttribute normalAttribute;
    normalAttribute.name = BufferAttributeID::NORMAL;
    normalAttribute.componentType = FLOAT_TYPE;
    normalAttribute.type = "VEC3";
    normalAttribute.offset = 12;
    bufferLayout.buffer_attribs.push_back(normalAttribute);

    BufferAttribute uvAttribute;
    uvAttribute.name = BufferAttributeID::TEXCOORD_0;
    uvAttribute.componentType = FLOAT_TYPE;
    uvAttribute.type = "VEC2";
    uvAttribute.offset = 24;
    bufferLayout.buffer_attribs.push_back(uvAttribute);

    bufferLayout.calculateVertexSize();
    params.bufferLayout = bufferLayout;

    std::vector<float> vertices;
    std::vector<uint16_t> indices;

    // Generate sphere vertices using spherical coordinates
    for (uint32_t lat = 0; lat <= segments; ++lat) {
        float theta = lat * M_PI / segments; // Latitude angle
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (uint32_t lon = 0; lon <= segments; ++lon) {
            float phi = lon * 2 * M_PI / segments; // Longitude angle
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            // Position
            float x = radius * sinTheta * cosPhi;
            float y = radius * cosTheta;
            float z = radius * sinTheta * sinPhi;

            // Normal (normalized position for unit sphere)
            float nx = sinTheta * cosPhi;
            float ny = cosTheta;
            float nz = sinTheta * sinPhi;

            // UV coordinates
            float u = (float)lon / segments;
            float v = (float)lat / segments;

            vertices.insert(vertices.end(), {x, y, z, nx, ny, nz, u, v});
        }
    }

    // Generate indices with correct winding order (counter-clockwise)
    for (uint16_t lat = 0; lat < segments; ++lat) {
        for (uint16_t lon = 0; lon < segments; ++lon) {
            uint16_t first = lat * (segments + 1) + lon;
            uint16_t second = first + segments + 1;

            // First triangle (counter-clockwise)
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            // Second triangle (counter-clockwise)
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }

    params.vertexData = vertices.data();
    params.vertexDataSize = vertices.size() * sizeof(float);
    params.indexData = indices.data();
    params.indexDataSize = indices.size() * sizeof(uint16_t);
    params.indexCount = indices.size();
    params.indexType = UNSIGNED_SHORT_TYPE;

    return Mesh(params);
}

Mesh Primitives::CreatePlane(float segments)
{
    AllocatorParams params;

    // Setup buffer layout for plane (position, normal, UV)
    BufferLayout bufferLayout;
    BufferAttribute positionAttribute;
    positionAttribute.name = BufferAttributeID::POSITION;
    positionAttribute.componentType = FLOAT_TYPE;
    positionAttribute.type = "VEC3";
    positionAttribute.offset = 0;
    bufferLayout.buffer_attribs.push_back(positionAttribute);

    BufferAttribute normalAttribute;
    normalAttribute.name = BufferAttributeID::NORMAL;
    normalAttribute.componentType = FLOAT_TYPE;
    normalAttribute.type = "VEC3";
    normalAttribute.offset = 12;
    bufferLayout.buffer_attribs.push_back(normalAttribute);

    BufferAttribute uvAttribute;
    uvAttribute.name = BufferAttributeID::TEXCOORD_0;
    uvAttribute.componentType = FLOAT_TYPE;
    uvAttribute.type = "VEC2";
    uvAttribute.offset = 24;
    bufferLayout.buffer_attribs.push_back(uvAttribute);

    bufferLayout.calculateVertexSize();
    params.bufferLayout = bufferLayout;

    std::vector<float> vertices;
    std::vector<uint16_t> indices;

    // Generate plane vertices
    uint32_t segs = static_cast<uint32_t>(segments);
    for (uint32_t i = 0; i <= segs; ++i) {
        for (uint32_t j = 0; j <= segs; ++j) {
            float x = (float)j / segs - 0.5f; // -0.5 to 0.5
            float z = (float)i / segs - 0.5f; // -0.5 to 0.5
            float y = 0.0f;

            // Normal pointing up
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;

            // UV coordinates
            float u = (float)j / segs;
            float v = (float)i / segs;

            vertices.insert(vertices.end(), {x, y, z, nx, ny, nz, u, v});
        }
    }

    // Generate indices with correct winding order (counter-clockwise)
    for (uint16_t i = 0; i < segs; ++i) {
        for (uint16_t j = 0; j < segs; ++j) {
            uint16_t first = i * (segs + 1) + j;
            uint16_t second = first + segs + 1;

            // First triangle (counter-clockwise)
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            // Second triangle (counter-clockwise)
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }

    params.vertexData = vertices.data();
    params.vertexDataSize = vertices.size() * sizeof(float);
    params.indexData = indices.data();
    params.indexDataSize = indices.size() * sizeof(uint16_t);
    params.indexCount = indices.size();
    params.indexType = UNSIGNED_SHORT_TYPE;

    return Mesh(params);
}

Mesh Primitives::CreateLine(float start, float end)
{
    AllocatorParams params;

    // Setup buffer layout for line (only position needed)
    BufferLayout bufferLayout;
    BufferAttribute positionAttribute;
    positionAttribute.name = BufferAttributeID::POSITION;
    positionAttribute.componentType = FLOAT_TYPE;
    positionAttribute.type = "VEC3";
    positionAttribute.offset = 0;
    bufferLayout.buffer_attribs.push_back(positionAttribute);

    bufferLayout.calculateVertexSize();
    params.bufferLayout = bufferLayout;

    // Simple line along X-axis from start to end
    std::vector<float> vertices = {
        start, 0.0f, 0.0f, // Start point
        end,   0.0f, 0.0f  // End point
    };

    std::vector<uint16_t> indices = {
        0, 1 // Line indices
    };

    params.vertexData = vertices.data();
    params.vertexDataSize = vertices.size() * sizeof(float);
    params.indexData = indices.data();
    params.indexDataSize = indices.size() * sizeof(uint16_t);
    params.indexCount = indices.size();
    params.indexType = UNSIGNED_SHORT_TYPE;

    return Mesh(params);
}

} // namespace Rapture