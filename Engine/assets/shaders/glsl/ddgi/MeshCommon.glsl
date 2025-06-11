struct MeshInfo {
    uint     RootIndex; // index of the root node in the BVH
    uint64_t AlbedoTextureHandle;
    uint64_t NormalTextureHandle;
    uint64_t MetallicRoughnessTextureHandle;
    uint     bufferMetadataIDX; // index for BufferMetadata array

    uint     vertexOffsetBytes;
    uint     indexOffsetBytes;

    mat4     Transform;
    mat4     InvTransform;
};

struct BufferMetadata {
    uint     positionAttributeOffsetBytes; // Offset of position *within* the stride
    uint     texCoordAttributeOffsetBytes;
    uint     normalAttributeOffsetBytes;
    uint     tangentAttributeOffsetBytes;


    uint     vertexStrideBytes;            // Stride of the vertex buffer in bytes
    uint     indexType;                    // GL_UNSIGNED_INT (5125) or GL_UNSIGNED_SHORT (5123)

    uint64_t VBOHandle;
    uint64_t IBOHandle;
};

struct Triangle {
    vec3 v0;
    vec3 v1;
    vec3 v2;  // Vertices in world space
    vec3 n0;
    vec3 n1;
    vec3 n2;  // Normals in world space
    vec3 t0;
    vec3 t1;
    vec3 t2;  // Tangents in world space
    vec2 uv0;
    vec2 uv1;
    vec2 uv2; // Texture coordinates
};

struct TriangleVertices {
    vec3 v0, v1, v2;
};

#ifdef UNSIGNED_INT
#else
    #define UNSIGNED_INT 5125
#endif

#ifdef UNSIGNED_SHORT
#else
    #define UNSIGNED_SHORT 5123
#endif

// meshinfo contains needed metadata about where to get the vertex data, the index is for the specific triangle
Triangle getTriangle(MeshInfo meshInfo, uint primitiveIndex, BufferMetadata bufferMetadata) {
    Triangle tri = Triangle(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec2(0.0), vec2(0.0), vec2(0.0));
    uint64_t vboHandle = bufferMetadata.VBOHandle;
    uint64_t iboHandle = bufferMetadata.IBOHandle;
    uint indexType = bufferMetadata.indexType;
    uint indexSize = indexType == UNSIGNED_INT ? 4 : 2;


    uint indexOffsetBytes = meshInfo.indexOffsetBytes + (indexSize * primitiveIndex * 3);

    uint indices[3];

    uint8_t *iboPtr = (uint8_t *)iboHandle;


    // Read indices based on indexType
    if (indexType == UNSIGNED_INT) { // GL_UNSIGNED_INT

        indices[0] = *((uint *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        indices[1] = *((uint *)(iboPtr + indexOffsetBytes + 4)); // Offset 16 is 4-byte aligned
        indices[2] = *((uint *)(iboPtr + indexOffsetBytes + 8)); // Offset 20 is 4-byte aligned

    } else { // Assuming GL_UNSIGNED_SHORT (5123)

        uint16_t ind0 = *((uint16_t *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        uint16_t ind1 = *((uint16_t *)(iboPtr + indexOffsetBytes + 2)); // Offset 16 is 4-byte aligned
        uint16_t ind2 = *((uint16_t *)(iboPtr + indexOffsetBytes + 4)); // Offset 20 is 4-byte aligned
        indices[0] = uint(ind0);
        indices[1] = uint(ind1);
        indices[2] = uint(ind2);
    }

    uvec3 vertexStartByteOffset;
    vertexStartByteOffset[0] = meshInfo.vertexOffsetBytes + (indices[0] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[1] = meshInfo.vertexOffsetBytes + (indices[1] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[2] = meshInfo.vertexOffsetBytes + (indices[2] * bufferMetadata.vertexStrideBytes);

    // Calculate the absolute byte offset for the start of the position attribute within that vertex
    uvec3 positionStartByteOffset = vertexStartByteOffset + bufferMetadata.positionAttributeOffsetBytes;

    uvec3 normalStartByteOffset = vertexStartByteOffset + bufferMetadata.normalAttributeOffsetBytes;

    uvec3 tangentStartByteOffset = vertexStartByteOffset + bufferMetadata.tangentAttributeOffsetBytes;

    uvec3 textureStartByteOffset =vertexStartByteOffset + bufferMetadata.texCoordAttributeOffsetBytes;

    uint8_t *vboBasePtr = (uint8_t *)vboHandle;

    vec3 v0_local;
    v0_local.x = *((float *)(vboBasePtr + positionStartByteOffset[0] + 0)); // Offset 12 is 4-byte aligned
    v0_local.y = *((float *)(vboBasePtr + positionStartByteOffset[0] + 4)); // Offset 16 is 4-byte aligned
    v0_local.z = *((float *)(vboBasePtr + positionStartByteOffset[0] + 8)); // Offset 20 is 4-byte aligned

    vec3 n0_local;
    n0_local.x = *((float *)(vboBasePtr + normalStartByteOffset[0] + 0)); // Offset 12 is 4-byte aligned
    n0_local.y = *((float *)(vboBasePtr + normalStartByteOffset[0] + 4)); // Offset 16 is 4-byte aligned
    n0_local.z = *((float *)(vboBasePtr + normalStartByteOffset[0] + 8)); // Offset 20 is 4-byte aligned

    vec3 t0_local;
    t0_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 0)); // Offset 12 is 4-byte aligned
    t0_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 4)); // Offset 16 is 4-byte aligned
    t0_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv0;
    uv0.x = *((float *)(vboBasePtr + textureStartByteOffset[0] + 0));
    uv0.y = *((float *)(vboBasePtr + textureStartByteOffset[0] + 4));
    
    // --- Vertex 1 ---
    vec3 v1_local;
    v1_local.x = *((float *)(vboBasePtr + positionStartByteOffset[1] + 0));
    v1_local.y = *((float *)(vboBasePtr + positionStartByteOffset[1] + 4));
    v1_local.z = *((float *)(vboBasePtr + positionStartByteOffset[1] + 8));

    vec3 n1_local;
    n1_local.x = *((float *)(vboBasePtr + normalStartByteOffset[1] + 0)); // Offset 12 is 4-byte aligned
    n1_local.y = *((float *)(vboBasePtr + normalStartByteOffset[1] + 4)); // Offset 16 is 4-byte aligned
    n1_local.z = *((float *)(vboBasePtr + normalStartByteOffset[1] + 8)); // Offset 20 is 4-byte aligned

    vec3 t1_local;
    t1_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 0)); // Offset 12 is 4-byte aligned
    t1_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 4)); // Offset 16 is 4-byte aligned
    t1_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv1;
    uv1.x = *((float *)(vboBasePtr + textureStartByteOffset[1] + 0));
    uv1.y = *((float *)(vboBasePtr + textureStartByteOffset[1] + 4));

    // --- Vertex 2 ---
    vec3 v2_local;
    v2_local.x = *((float *)(vboBasePtr + positionStartByteOffset[2] + 0));
    v2_local.y = *((float *)(vboBasePtr + positionStartByteOffset[2] + 4));
    v2_local.z = *((float *)(vboBasePtr + positionStartByteOffset[2] + 8));
    
    vec3 n2_local;
    n2_local.x = *((float *)(vboBasePtr + normalStartByteOffset[2] + 0)); // Offset 12 is 4-byte aligned
    n2_local.y = *((float *)(vboBasePtr + normalStartByteOffset[2] + 4)); // Offset 16 is 4-byte aligned
    n2_local.z = *((float *)(vboBasePtr + normalStartByteOffset[2] + 8)); // Offset 20 is 4-byte aligned
    
    vec3 t2_local;
    t2_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 0)); // Offset 12 is 4-byte aligned
    t2_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 4)); // Offset 16 is 4-byte aligned
    t2_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv2;
    uv2.x = *((float *)(vboBasePtr + textureStartByteOffset[2] + 0));
    uv2.y = *((float *)(vboBasePtr + textureStartByteOffset[2] + 4));

    
    tri.v0 = (meshInfo.Transform * vec4(v0_local, 1.0)).xyz;
    tri.v1 = (meshInfo.Transform * vec4(v1_local, 1.0)).xyz;
    tri.v2 = (meshInfo.Transform * vec4(v2_local, 1.0)).xyz;

    tri.n0 = (meshInfo.Transform * vec4(n0_local, 0.0)).xyz;
    tri.n1 = (meshInfo.Transform * vec4(n1_local, 0.0)).xyz;
    tri.n2 = (meshInfo.Transform * vec4(n2_local, 0.0)).xyz;

    tri.t0 = (meshInfo.Transform * vec4(t0_local, 0.0)).xyz;
    tri.t1 = (meshInfo.Transform * vec4(t1_local, 0.0)).xyz;
    tri.t2 = (meshInfo.Transform * vec4(t2_local, 0.0)).xyz;

    
    tri.uv0 = uv0;
    tri.uv1 = uv1;
    tri.uv2 = uv2;
    

    return tri;
}

// returns just the vertices in local space
TriangleVertices getTriangleVertices(MeshInfo meshInfo, uint primitiveIndex, BufferMetadata bufferMetadata) {
    TriangleVertices tri = TriangleVertices(vec3(0.0), vec3(0.0), vec3(0.0));
    uint64_t vboHandle = bufferMetadata.VBOHandle;
    uint64_t iboHandle = bufferMetadata.IBOHandle;
    uint indexType = bufferMetadata.indexType;
    uint indexSize = indexType == UNSIGNED_INT ? 4 : 2;


    uint indexOffsetBytes = meshInfo.indexOffsetBytes + (indexSize * primitiveIndex * 3);

    uint indices[3];

    uint8_t *iboPtr = (uint8_t *)iboHandle;


    // Read indices based on indexType
    if (indexType == UNSIGNED_INT) { // GL_UNSIGNED_INT

        indices[0] = *((uint *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        indices[1] = *((uint *)(iboPtr + indexOffsetBytes + 4)); // Offset 16 is 4-byte aligned
        indices[2] = *((uint *)(iboPtr + indexOffsetBytes + 8)); // Offset 20 is 4-byte aligned

    } else { // Assuming GL_UNSIGNED_SHORT (5123)

        uint16_t ind0 = *((uint16_t *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        uint16_t ind1 = *((uint16_t *)(iboPtr + indexOffsetBytes + 2)); // Offset 16 is 4-byte aligned
        uint16_t ind2 = *((uint16_t *)(iboPtr + indexOffsetBytes + 4)); // Offset 20 is 4-byte aligned
        indices[0] = uint(ind0);
        indices[1] = uint(ind1);
        indices[2] = uint(ind2);
    }

    uvec3 vertexStartByteOffset;
    vertexStartByteOffset[0] = meshInfo.vertexOffsetBytes + (indices[0] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[1] = meshInfo.vertexOffsetBytes + (indices[1] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[2] = meshInfo.vertexOffsetBytes + (indices[2] * bufferMetadata.vertexStrideBytes);

    // Calculate the absolute byte offset for the start of the position attribute within that vertex
    uvec3 positionStartByteOffset = vertexStartByteOffset + bufferMetadata.positionAttributeOffsetBytes;




    float32_t *vboBasePtr = (float32_t *)vboHandle;

    float32_t* offsetAddress = vboBasePtr + positionStartByteOffset[0]/4;
    tri.v0.x = *((float *)(offsetAddress + 0)); // Offset 12 is 4-byte aligned
    tri.v0.y = *((float *)(offsetAddress + 1)); // Offset 16 is 4-byte aligned
    tri.v0.z = *((float *)(offsetAddress + 2)); // Offset 20 is 4-byte aligned

    // --- Vertex 1 ---
    offsetAddress = vboBasePtr + positionStartByteOffset[1]/4;
    tri.v1.x = *((float *)(offsetAddress + 0));
    tri.v1.y = *((float *)(offsetAddress + 1));
    tri.v1.z = *((float *)(offsetAddress + 2));

    // --- Vertex 2 ---
    offsetAddress = vboBasePtr + positionStartByteOffset[2]/4;
    tri.v2.x = *((float *)(offsetAddress + 0));
    tri.v2.y = *((float *)(offsetAddress + 1));
    tri.v2.z = *((float *)(offsetAddress + 2));
    

    return tri;
}

// fetches the remaining data for a triangle, given the vertices and transforms them to WORLD SPACE
Triangle getTriangleExtras(MeshInfo meshInfo, uint primitiveIndex, TriangleVertices triVertices, BufferMetadata bufferMetadata) {
    Triangle tri = Triangle(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec2(0.0), vec2(0.0), vec2(0.0));
    uint64_t vboHandle = bufferMetadata.VBOHandle;
    uint64_t iboHandle = bufferMetadata.IBOHandle;
    uint indexType = bufferMetadata.indexType;
    uint indexSize = indexType == UNSIGNED_INT ? 4 : 2;


    uint indexOffsetBytes = meshInfo.indexOffsetBytes + (indexSize * primitiveIndex * 3);

    uint indices[3];

    uint8_t *iboPtr = (uint8_t *)iboHandle;


    // Read indices based on indexType
    if (indexType == UNSIGNED_INT) { // GL_UNSIGNED_INT

        indices[0] = *((uint *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        indices[1] = *((uint *)(iboPtr + indexOffsetBytes + 4)); // Offset 16 is 4-byte aligned
        indices[2] = *((uint *)(iboPtr + indexOffsetBytes + 8)); // Offset 20 is 4-byte aligned

    } else { // Assuming GL_UNSIGNED_SHORT (5123)

        uint16_t ind0 = *((uint16_t *)(iboPtr + indexOffsetBytes + 0)); // Offset 12 is 4-byte aligned
        uint16_t ind1 = *((uint16_t *)(iboPtr + indexOffsetBytes + 2)); // Offset 16 is 4-byte aligned
        uint16_t ind2 = *((uint16_t *)(iboPtr + indexOffsetBytes + 4)); // Offset 20 is 4-byte aligned
        indices[0] = uint(ind0);
        indices[1] = uint(ind1);
        indices[2] = uint(ind2);
    }

    uvec3 vertexStartByteOffset;
    vertexStartByteOffset[0] = meshInfo.vertexOffsetBytes + (indices[0] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[1] = meshInfo.vertexOffsetBytes + (indices[1] * bufferMetadata.vertexStrideBytes);
    vertexStartByteOffset[2] = meshInfo.vertexOffsetBytes + (indices[2] * bufferMetadata.vertexStrideBytes);


    uvec3 normalStartByteOffset = vertexStartByteOffset + bufferMetadata.normalAttributeOffsetBytes;

    uvec3 tangentStartByteOffset = vertexStartByteOffset + bufferMetadata.tangentAttributeOffsetBytes;

    uvec3 textureStartByteOffset =vertexStartByteOffset + bufferMetadata.texCoordAttributeOffsetBytes;

    uint8_t *vboBasePtr = (uint8_t *)vboHandle;


    vec3 n0_local;
    n0_local.x = *((float *)(vboBasePtr + normalStartByteOffset[0] + 0)); // Offset 12 is 4-byte aligned
    n0_local.y = *((float *)(vboBasePtr + normalStartByteOffset[0] + 4)); // Offset 16 is 4-byte aligned
    n0_local.z = *((float *)(vboBasePtr + normalStartByteOffset[0] + 8)); // Offset 20 is 4-byte aligned

    vec3 t0_local;
    t0_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 0)); // Offset 12 is 4-byte aligned
    t0_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 4)); // Offset 16 is 4-byte aligned
    t0_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[0] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv0;
    uv0.x = *((float *)(vboBasePtr + textureStartByteOffset[0] + 0));
    uv0.y = *((float *)(vboBasePtr + textureStartByteOffset[0] + 4));
    
    // --- Vertex 1 ---
    vec3 n1_local;
    n1_local.x = *((float *)(vboBasePtr + normalStartByteOffset[1] + 0)); // Offset 12 is 4-byte aligned
    n1_local.y = *((float *)(vboBasePtr + normalStartByteOffset[1] + 4)); // Offset 16 is 4-byte aligned
    n1_local.z = *((float *)(vboBasePtr + normalStartByteOffset[1] + 8)); // Offset 20 is 4-byte aligned

    vec3 t1_local;
    t1_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 0)); // Offset 12 is 4-byte aligned
    t1_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 4)); // Offset 16 is 4-byte aligned
    t1_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[1] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv1;
    uv1.x = *((float *)(vboBasePtr + textureStartByteOffset[1] + 0));
    uv1.y = *((float *)(vboBasePtr + textureStartByteOffset[1] + 4));

    // --- Vertex 2 ---
    vec3 n2_local;
    n2_local.x = *((float *)(vboBasePtr + normalStartByteOffset[2] + 0)); // Offset 12 is 4-byte aligned
    n2_local.y = *((float *)(vboBasePtr + normalStartByteOffset[2] + 4)); // Offset 16 is 4-byte aligned
    n2_local.z = *((float *)(vboBasePtr + normalStartByteOffset[2] + 8)); // Offset 20 is 4-byte aligned
    
    vec3 t2_local;
    t2_local.x = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 0)); // Offset 12 is 4-byte aligned
    t2_local.y = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 4)); // Offset 16 is 4-byte aligned
    t2_local.z = *((float *)(vboBasePtr + tangentStartByteOffset[2] + 8)); // Offset 20 is 4-byte aligned

    vec2 uv2;
    uv2.x = *((float *)(vboBasePtr + textureStartByteOffset[2] + 0));
    uv2.y = *((float *)(vboBasePtr + textureStartByteOffset[2] + 4));

    
    tri.v0 = (meshInfo.Transform * vec4(triVertices.v0, 1.0)).xyz;
    tri.v1 = (meshInfo.Transform * vec4(triVertices.v1, 1.0)).xyz;
    tri.v2 = (meshInfo.Transform * vec4(triVertices.v2, 1.0)).xyz;

    tri.n0 = (meshInfo.Transform * vec4(n0_local, 0.0)).xyz;
    tri.n1 = (meshInfo.Transform * vec4(n1_local, 0.0)).xyz;
    tri.n2 = (meshInfo.Transform * vec4(n2_local, 0.0)).xyz;

    tri.t0 = (meshInfo.Transform * vec4(t0_local, 0.0)).xyz;
    tri.t1 = (meshInfo.Transform * vec4(t1_local, 0.0)).xyz;
    tri.t2 = (meshInfo.Transform * vec4(t2_local, 0.0)).xyz;

    
    tri.uv0 = uv0;
    tri.uv1 = uv1;
    tri.uv2 = uv2;
    

    return tri;
}