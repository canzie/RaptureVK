struct MeshInfo {
    uint     RootIndex; // index of the root node in the BVH
    uint     AlbedoTextureIndex;
    uint     NormalTextureIndex;
    uint     MetallicRoughnessTextureIndex;
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

    uint     VBOIndex;
    uint     IBOIndex;
};



#ifdef UNSIGNED_INT
#else
    #define UNSIGNED_INT 5125
#endif

#ifdef UNSIGNED_SHORT
#else
    #define UNSIGNED_SHORT 5123
#endif
