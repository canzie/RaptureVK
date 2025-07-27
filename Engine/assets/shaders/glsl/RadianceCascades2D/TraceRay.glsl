
const uint UINT32_MAX = 0xFFFFFFFFu;

layout(set = 3, binding = 2) uniform accelerationStructureEXT topLevelAS[];


layout(set = 3, binding = 0) uniform sampler2D gTexturesINC[];


// Bindless buffers (same as DDGI)
layout(set = 3, binding = 1) readonly buffer gBindlessBuffers {
    uint data[];
} gBuffers[];

// Simplified MeshInfo structure matching our C++ version (same as DDGI)
struct MeshInfo {
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    vec3 albedo;
    vec3 emissiveColor;
    uint EmissiveFactorTextureIndex;
    
    uint iboIndex; // index of the buffer in the bindless buffers array
    uint vboIndex; // index of the buffer in the bindless buffers array
    uint meshIndex; // index of the mesh in the mesh array, this is the same index as the tlasinstance instanceCustomIndex

    mat4 modelMatrix;

    uint     positionAttributeOffsetBytes; // Offset of position *within* the stride
    uint     texCoordAttributeOffsetBytes;
    uint     normalAttributeOffsetBytes;
    uint     tangentAttributeOffsetBytes;

    uint     vertexStrideBytes;            // Stride of the vertex buffer in bytes
    uint     indexType;                    // GL_UNSIGNED_INT (5125) or GL_UNSIGNED_SHORT (5123)
};

// Scene info buffer (reuse DDGI binding)
layout(std430, set=3, binding = 9) readonly buffer SceneInfo {
    MeshInfo MeshInfos[];
} u_sceneInfo;


// Vertex attribute structure for clean data passing
struct VertexAttributes {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
    vec4 tangent;
    vec3 bitangent;
};

// Interpolated surface data
struct SurfaceData {
    vec3 position;
    vec2 texCoord;
    vec3 normal;
    vec4 tangent;
    vec3 bitangent;
};


/**
 * Fetches triangle indices from the index buffer
 */
uvec3 fetchTriangleIndices(uint primitiveID, MeshInfo meshInfo) {
    uint baseIndex = primitiveID * 3;
    uvec3 indices;
    
    if (meshInfo.indexType == 5125) { // GL_UNSIGNED_INT
        uint indexOffset = baseIndex * 4; // 4 bytes per uint
        indices.x = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        indices.y = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];
        indices.z = gBuffers[meshInfo.iboIndex].data[(indexOffset + 8) / 4];
    } else { // GL_UNSIGNED_SHORT (5123)
        uint indexOffset = baseIndex * 2; // 2 bytes per ushort
        uint packedIndices0 = gBuffers[meshInfo.iboIndex].data[indexOffset / 4];
        uint packedIndices1 = gBuffers[meshInfo.iboIndex].data[(indexOffset + 4) / 4];
        
        // Extract 16-bit indices from packed 32-bit values
        indices.x = packedIndices0 & 0xFFFF;
        indices.y = (packedIndices0 >> 16) & 0xFFFF;
        indices.z = packedIndices1 & 0xFFFF;
    }
    
    return indices;
}

/**
 * Fetches vertex attributes for a single vertex
 */
VertexAttributes fetchVertexAttributes(uint vertexIndex, MeshInfo meshInfo) {
    VertexAttributes attrs;
    uint vertexOffset = vertexIndex * meshInfo.vertexStrideBytes;

    // === Position (required) ===
    vec3 position = vec3(0.0);
    if(meshInfo.positionAttributeOffsetBytes != UINT32_MAX) {
        uint posOffset = vertexOffset + meshInfo.positionAttributeOffsetBytes;
        position = vec3(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[posOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(posOffset + 8) / 4])
        );
    }
    attrs.position = (meshInfo.modelMatrix * vec4(position, 1.0)).xyz;

    // === Texture coordinates (optional) ===
    if(meshInfo.texCoordAttributeOffsetBytes != UINT32_MAX) {
        uint texOffset = vertexOffset + meshInfo.texCoordAttributeOffsetBytes;
        attrs.texCoord = vec2(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[texOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(texOffset + 4) / 4])
        );
    } else {
        attrs.texCoord = vec2(0.0);
    }

    // === Normal (optional but preferred) ===
    vec3 normal = vec3(0.0, 0.0, 1.0);
    if(meshInfo.normalAttributeOffsetBytes != UINT32_MAX) {
        uint normalOffset = vertexOffset + meshInfo.normalAttributeOffsetBytes;
        normal = vec3(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[normalOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(normalOffset + 8) / 4])
        );
    }
    attrs.normal = normalize((meshInfo.modelMatrix * vec4(normal, 0.0)).xyz);

    // === Tangent (optional) - only compute if normal mapping might be used ===
    vec4 tangent = vec4(0.0);
    if(meshInfo.tangentAttributeOffsetBytes != UINT32_MAX) {
        uint tangentOffset = vertexOffset + meshInfo.tangentAttributeOffsetBytes;
        vec4 localTangent = vec4(
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[tangentOffset / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 4) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 8) / 4]),
            uintBitsToFloat(gBuffers[meshInfo.vboIndex].data[(tangentOffset + 12) / 4])
        );
        vec3 worldTangent = normalize((meshInfo.modelMatrix * vec4(localTangent.xyz, 0.0)).xyz);
        tangent = vec4(worldTangent, localTangent.w);
        
        // Re-orthogonalize tangent with respect to normal (in world space)
        vec3 T = normalize(tangent.xyz - dot(tangent.xyz, attrs.normal) * attrs.normal);
        attrs.tangent = vec4(T, tangent.w);

        // Calculate bitangent with proper handedness
        attrs.bitangent = normalize(cross(attrs.normal, T) * attrs.tangent.w);
    } else {
        // When no tangent data is provided, just set dummy values
        // These will be ignored if no normal mapping is used
        attrs.tangent = vec4(1.0, 0.0, 0.0, 1.0);
        attrs.bitangent = vec3(0.0, 1.0, 0.0);
    }

    return attrs;
}

/**
 * Interpolates vertex attributes using barycentric coordinates
 */
SurfaceData interpolateVertexAttributes(VertexAttributes v0, VertexAttributes v1, VertexAttributes v2, vec2 barycentrics) {
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    
    SurfaceData surface;
    surface.position = v0.position * weights.x + v1.position * weights.y + v2.position * weights.z;
    surface.texCoord = v0.texCoord * weights.x + v1.texCoord * weights.y + v2.texCoord * weights.z;
    surface.normal = normalize(v0.normal * weights.x + v1.normal * weights.y + v2.normal * weights.z);
    
    vec4 interpolatedTangent = v0.tangent * weights.x + v1.tangent * weights.y + v2.tangent * weights.z;
    surface.tangent = vec4(normalize(interpolatedTangent.xyz), interpolatedTangent.w);
    
    // Interpolate bitangent
    surface.bitangent = normalize(v0.bitangent * weights.x + v1.bitangent * weights.y + v2.bitangent * weights.z);
    
    // Calculate bitangent using the same approach as GBuffer shader
    // Re-orthogonalize tangent with respect to normal (in world space)
    vec3 T = normalize(surface.tangent.xyz - dot(surface.tangent.xyz, surface.normal) * surface.normal);
    surface.tangent = vec4(T, surface.tangent.w);
    
    // Calculate bitangent with proper handedness
    surface.bitangent = normalize(cross(surface.normal, T) * surface.tangent.w);
    
    return surface;
}

/**
 * Gets complete surface data for a ray hit
 */
SurfaceData getSurfaceDataForHit(uint primitiveID, vec2 barycentrics, MeshInfo meshInfo) {
    // Fetch triangle indices
    uvec3 indices = fetchTriangleIndices(primitiveID, meshInfo);
    
    // Fetch vertex data for all 3 vertices
    VertexAttributes v0 = fetchVertexAttributes(indices.x, meshInfo);
    VertexAttributes v1 = fetchVertexAttributes(indices.y, meshInfo);
    VertexAttributes v2 = fetchVertexAttributes(indices.z, meshInfo);
    
    // Interpolate attributes
    return interpolateVertexAttributes(v0, v1, v2, barycentrics);
}

vec3 sampleAlbedo(MeshInfo meshInfo, vec2 uv) {
    uint texIndex = meshInfo.AlbedoTextureIndex;
    vec3 baseColor = meshInfo.albedo;

    if(texIndex == UINT32_MAX) {
        return baseColor; 
    }

    return baseColor * texture(gTexturesINC[texIndex], uv).rgb;
}

vec3 sampleEmissiveColor(MeshInfo meshInfo, vec2 uv) {
    uint texIndex = meshInfo.EmissiveFactorTextureIndex;
    if(texIndex == UINT32_MAX) {
        return meshInfo.emissiveColor;
    }
    return texture(gTexturesINC[texIndex], uv).rgb * meshInfo.emissiveColor;
}
