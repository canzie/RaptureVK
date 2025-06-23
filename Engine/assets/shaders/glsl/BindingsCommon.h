
// ==============  SET 0 ==============
#define S0_B0_CAMERA_UBO 0              // camera data like projection and view matrices
#define S0_B1_LIGHTS_UBO 1              // light data like position, direction, color, type, etc.
#define S0_B2_SHADOW_MATRICES_UBO 2     // Shadow lightviewmatrix for regular shadow maps
#define S0_B3_CASCADE_MATRICES_UBO 3    // Shadow lightviewmatrices for cascaded shadow maps (n matrices instead of 1)
#define S0_B4_SHADOW_DATA_UBO 4         // Shadow data like shadow map handles, cascade splits, etc.


// ==============  SET 1 ==============
#define S1_B0_MATERIAL_UBO 0           // material data specific to each material, data like albedo, roughness, metallic, albedo map index, etc.


// ==============  SET 2 ==============
#define S2_B0_MESH_DATA_UBO 0          // model matrix, flags, ...


// ==============  SET 3 ==============
#define S3_B0_BINDLESS_TEXTURES 0      // bindless texture handles
#define S3_B1_BINDLESS_SSBOS 1         // bindless shadow array handles

