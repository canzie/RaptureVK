#include "glTFLoader.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <yyjson.h>

#include "AssetManager/AssetManager.h"
#include "Buffers/VertexBuffers/BufferLayout.h"
#include "Components/Components.h"
#include "Components/HierarchyComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameters.h"
#include "Meshes/Mesh.h"
#include "Scenes/Entities/Entity.h"

namespace Rapture {

glTF2Loader::glTF2Loader(const std::filesystem::path &filepath) : m_filepath(filepath)
{
    m_basePath = filepath.parent_path().string();
    if (!m_basePath.empty() && m_basePath.back() != '/' && m_basePath.back() != '\\') {
        m_basePath += '/';
    }
}

glTF2Loader::~glTF2Loader()
{
    cleanUp();
}

// Helper functions for yyjson
yyjson_val *glTF2Loader::getObjectValue(yyjson_val *obj, const char *key)
{
    if (!obj || !yyjson_is_obj(obj)) return nullptr;
    return yyjson_obj_get(obj, key);
}

yyjson_val *glTF2Loader::getArrayElement(yyjson_val *arr, uint32_t index)
{
    if (!arr || !yyjson_is_arr(arr)) return nullptr;
    return yyjson_arr_get(arr, index);
}

const char *glTF2Loader::getString(yyjson_val *val, const char *defaultValue)
{
    if (!val || !yyjson_is_str(val)) return defaultValue;
    return yyjson_get_str(val);
}

int glTF2Loader::getInt(yyjson_val *val, int defaultValue)
{
    if (!val) return defaultValue;
    if (yyjson_is_int(val)) return yyjson_get_int(val);
    if (yyjson_is_uint(val)) return (int)yyjson_get_uint(val);
    return defaultValue;
}

double glTF2Loader::getDouble(yyjson_val *val, double defaultValue)
{
    if (!val) return defaultValue;
    if (yyjson_is_real(val)) return yyjson_get_real(val);
    if (yyjson_is_int(val)) return (double)yyjson_get_int(val);
    if (yyjson_is_uint(val)) return (double)yyjson_get_uint(val);
    return defaultValue;
}

bool glTF2Loader::getBool(yyjson_val *val, bool defaultValue)
{
    if (!val || !yyjson_is_bool(val)) return defaultValue;
    return yyjson_get_bool(val);
}

size_t glTF2Loader::getArraySize(yyjson_val *arr)
{
    if (!arr || !yyjson_is_arr(arr)) return 0;
    return yyjson_arr_size(arr);
}

void glTF2Loader::loadAndSetTexture(MaterialInstance *material, ParameterID id, int textureIndex)
{
    if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= getArraySize(m_textures)) {
        RP_CORE_ERROR("glTF2Loader: Invalid texture index {}", textureIndex);
        return;
    }

    yyjson_val *texture = getArrayElement(m_textures, textureIndex);

    // Get the image index
    yyjson_val *sourceVal = getObjectValue(texture, "source");
    if (!sourceVal) {
        RP_CORE_ERROR("Texture missing source property");
        return;
    }

    int imageIndex = getInt(sourceVal, -1);
    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= getArraySize(m_images)) {
        RP_CORE_ERROR("Invalid image index {}", imageIndex);
        return;
    }

    yyjson_val *image = getArrayElement(m_images, imageIndex);

    // Get the image URI
    yyjson_val *uriVal = getObjectValue(image, "uri");
    if (!uriVal) {
        RP_CORE_ERROR("Image missing URI");
        return;
    }

    std::string imageURI = getString(uriVal, "");

    // Load the texture
    std::string texturePath = m_basePath + imageURI;
    std::filesystem::path texturePathFS = std::filesystem::path(texturePath);

    TextureImportConfig texImportConfig;
    if (id == ParameterID::ALBEDO_MAP) {
        texImportConfig.srgb = true;
    } else if (id == ParameterID::METALLIC_ROUGHNESS_MAP) {
        texImportConfig.srgb = false;
    } else if (id == ParameterID::NORMAL_MAP) {
        texImportConfig.srgb = false;
    } else if (id == ParameterID::AO_MAP) {
        texImportConfig.srgb = false;
    } else if (id == ParameterID::EMISSIVE_MAP) {
        texImportConfig.srgb = true;
    }

    auto asset = AssetManager::importAsset(texturePathFS, texImportConfig);
    auto tex = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;

    if (!tex) {
        RP_CORE_ERROR("Failed to import or get texture {}", texturePath);
        return;
    }

    // Use a shared_ptr to store the listener ID so we can capture it in the lambda
    // AssetEvents::onAssetLoaded().addListener([id, tex, handle, material](AssetHandle _handle) {
    //    if (handle == _handle) {
    //        material->markDescriptorSetDirty();
    //    }
    //});

    // change this to use a callback?
    // Use the non-template overload which correctly converts Texture to uint32_t bindless index
    material->setParameter(id, tex);
}

bool glTF2Loader::load(Scene *scene, int32_t sceneIndex)
{
    m_isLoaded = false;
    cleanUp();

    m_loadedData = std::make_unique<glTF_LoadedSceneData>();

    std::ifstream gltfFile(m_filepath);
    if (!gltfFile) {
        RP_CORE_ERROR("Couldn't load glTF file '{}'", m_filepath.string());
        return false;
    }

    std::string gltfContent((std::istreambuf_iterator<char>(gltfFile)), std::istreambuf_iterator<char>());
    gltfFile.close();

    if (gltfContent.empty()) {
        RP_CORE_ERROR("Empty glTF file '{}'", m_filepath.string());
        return false;
    }

    yyjson_read_err err;
    m_glTFdoc = yyjson_read(gltfContent.c_str(), gltfContent.length(), 0);

    if (!m_glTFdoc) {
        RP_CORE_ERROR("Failed to parse glTF JSON: {} at position {}", err.msg, err.pos);
        return false;
    }

    m_glTFroot = yyjson_doc_get_root(m_glTFdoc);
    if (!m_glTFroot) {
        RP_CORE_ERROR("Failed to get root from glTF JSON");
        yyjson_doc_free(m_glTFdoc);
        m_glTFdoc = nullptr;
        return false;
    }

    m_accessors = getObjectValue(m_glTFroot, "accessors");
    m_meshes = getObjectValue(m_glTFroot, "meshes");
    m_bufferViews = getObjectValue(m_glTFroot, "bufferViews");
    m_buffers = getObjectValue(m_glTFroot, "buffers");
    m_nodes = getObjectValue(m_glTFroot, "nodes");
    m_materials = getObjectValue(m_glTFroot, "materials");
    m_animations = getObjectValue(m_glTFroot, "animations");
    m_skins = getObjectValue(m_glTFroot, "skins");
    m_textures = getObjectValue(m_glTFroot, "textures");
    m_images = getObjectValue(m_glTFroot, "images");
    m_samplers = getObjectValue(m_glTFroot, "samplers");

    if (!m_accessors || !m_meshes || !m_bufferViews || !m_buffers) {
        RP_CORE_ERROR("Missing required glTF sections");
        return false;
    }

    yyjson_val *firstBuffer = getArrayElement(m_buffers, 0);
    if (!firstBuffer) {
        RP_CORE_ERROR("No buffers found");
        return false;
    }

    const char *bufferURI = getString(getObjectValue(firstBuffer, "uri"), "");
    if (strlen(bufferURI) == 0) {
        RP_CORE_ERROR("Buffer URI is missing");
        return false;
    }

    std::string fullBufferPath;
    if (strstr(bufferURI, "://") == nullptr && strlen(bufferURI) > 0) {
        fullBufferPath = m_basePath + bufferURI;
    } else {
        fullBufferPath = bufferURI;
    }

    std::ifstream binaryFile(fullBufferPath, std::ios::binary);
    if (!binaryFile) {
        RP_CORE_ERROR("Couldn't load binary file '{}'", fullBufferPath);
        return false;
    }

    binaryFile.seekg(0, std::ios::end);
    size_t fileSize = binaryFile.tellg();
    binaryFile.seekg(0, std::ios::beg);

    m_binVec.resize(fileSize);

    if (!binaryFile.read(reinterpret_cast<char *>(m_binVec.data()), fileSize)) {
        RP_CORE_ERROR("Failed to read binary data");
        return false;
    }

    binaryFile.close();

    yyjson_val *scenes = getObjectValue(m_glTFroot, "scenes");
    if (scenes && getArraySize(scenes) > 0) {
        if (sceneIndex < 0 || sceneIndex >= static_cast<int32_t>(getArraySize(scenes))) {
            yyjson_val *sceneIndexVal = getObjectValue(m_glTFroot, "scene");
            sceneIndex = static_cast<int32_t>(getInt(sceneIndexVal, 0));
        }
        yyjson_val *sceneToProcess = getArrayElement(scenes, sceneIndex);
        if (sceneToProcess) {
            loadScene(sceneToProcess);
        }
    } else if (m_nodes && getArraySize(m_nodes) > 0) {
        for (size_t i = 0; i < getArraySize(m_nodes); ++i) {
            loadNode(nullptr, i);
        }
    }

    m_isInitialized = true;
    m_isLoaded = true;

    if (scene) {
        finalizeToScene(scene);
    }

    return true;
}

bool glTF2Loader::loadScene(yyjson_val *scene)
{
    yyjson_val *node;
    size_t idx, max;
    yyjson_val *nodes = getObjectValue(scene, "nodes");
    auto sceneNode = std::make_unique<glTF_SceneNode>();
    sceneNode->name = "Scene";
    yyjson_arr_foreach(nodes, idx, max, node)
    {
        loadNode(sceneNode.get(), idx);
    }

    m_loadedData->rootNodes.push_back(std::move(sceneNode));
    return true;
}

bool glTF2Loader::loadNode(glTF_SceneNode *parent, size_t idx)
{
    yyjson_val *nodeJson = getArrayElement(m_nodes, idx);
    if (!nodeJson) return false;

    auto node = std::make_unique<glTF_SceneNode>();
    node->parent = parent;
    node->name = getString(getObjectValue(nodeJson, "name"), "Node");

    glm::mat4 localTransform = getNodeTransform(nodeJson);
    node->worldTransform = (parent ? parent->worldTransform : glm::mat4(1.0f)) * localTransform;

    yyjson_val *meshIdxVal = getObjectValue(nodeJson, "mesh");
    if (meshIdxVal && yyjson_is_int(meshIdxVal)) {
        size_t meshIndex = static_cast<size_t>(getInt(meshIdxVal, 0));
        if (meshIndex < getArraySize(m_meshes)) {
            loadMesh(node.get(), meshIndex);
        }
    }

    yyjson_val *childrenVal = getObjectValue(nodeJson, "children");
    if (childrenVal && yyjson_is_arr(childrenVal)) {
        size_t childCount = getArraySize(childrenVal);
        for (size_t i = 0; i < childCount; i++) {
            yyjson_val *childIndexVal = getArrayElement(childrenVal, i);
            size_t childIndex = static_cast<size_t>(getInt(childIndexVal, 0));
            if (childIndex < getArraySize(m_nodes)) {
                loadNode(node.get(), childIndex);
            }
        }
    }

    yyjson_val *skinVal = getObjectValue(nodeJson, "skin");
    if (skinVal) {
        loadSkin(skinVal);
        node->type = glTF_NodeType::SKELETON;
    }

    yyjson_val *weightsVal = getObjectValue(nodeJson, "weights");
    if (weightsVal) {
        loadWeights(weightsVal);
    }

    if (parent) {
        parent->children.push_back(std::move(node));
    }
    return true;
}

bool glTF2Loader::loadMesh(glTF_SceneNode *node, size_t meshIndex)
{
    yyjson_val *meshJson = getArrayElement(m_meshes, static_cast<uint32_t>(meshIndex));
    if (!meshJson) return false;

    std::string meshName = getString(getObjectValue(meshJson, "name"), "");
    if (meshName.empty()) {
        meshName = "Mesh_" + std::to_string(meshIndex);
    }

    yyjson_val *primitivesVal = getObjectValue(meshJson, "primitives");
    if (!primitivesVal || !yyjson_is_arr(primitivesVal)) return false;

    size_t primitiveCount = getArraySize(primitivesVal);
    if (primitiveCount < 1) return false;

    size_t idx, max;
    yyjson_val *primitiveJson;
    yyjson_arr_foreach(primitivesVal, idx, max, primitiveJson)
    {
        loadPrimitive(node, primitiveJson, idx);
    }

    return true;
}

bool glTF2Loader::loadPrimitive(glTF_SceneNode *parent, yyjson_val *primitiveJson, size_t primitiveIndex)
{
    auto node = std::make_unique<glTF_SceneNode>();
    node->parent = parent;
    node->name = parent->name + "_Primitive_" + std::to_string(primitiveIndex);
    node->type = glTF_NodeType::PRIMITIVE;
    node->worldTransform = parent->worldTransform;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> attributeData;
    uint32_t vertexCount = 0;

    glm::vec3 minBounds(std::numeric_limits<float>::infinity());
    glm::vec3 maxBounds(-std::numeric_limits<float>::infinity());
    bool hasBounds = false;

    yyjson_val *attributesVal = getObjectValue(primitiveJson, "attributes");
    if (attributesVal && yyjson_is_obj(attributesVal)) {
        yyjson_obj_iter iter = yyjson_obj_iter_with(attributesVal);
        yyjson_val *key, *val;
        while ((key = yyjson_obj_iter_next(&iter))) {
            val = yyjson_obj_iter_get_val(key);
            const char *attribName = yyjson_get_str(key);
            if (strcmp(attribName, "COLOR_0") == 0) continue;

            size_t accessorIdx = static_cast<size_t>(getInt(val, 0));
            yyjson_val *accessor = getArrayElement(m_accessors, static_cast<uint32_t>(accessorIdx));

            if (strcmp(attribName, "POSITION") == 0 && accessor) {
                vertexCount = static_cast<uint32_t>(getInt(getObjectValue(accessor, "count"), 0));

                yyjson_val *minVal = getObjectValue(accessor, "min");
                yyjson_val *maxVal = getObjectValue(accessor, "max");
                if (minVal && yyjson_is_arr(minVal) && getArraySize(minVal) >= 3 && maxVal && yyjson_is_arr(maxVal) &&
                    getArraySize(maxVal) >= 3) {
                    minBounds = glm::vec3(static_cast<float>(getDouble(getArrayElement(minVal, 0), 0.0)),
                                          static_cast<float>(getDouble(getArrayElement(minVal, 1), 0.0)),
                                          static_cast<float>(getDouble(getArrayElement(minVal, 2), 0.0)));
                    maxBounds = glm::vec3(static_cast<float>(getDouble(getArrayElement(maxVal, 0), 0.0)),
                                          static_cast<float>(getDouble(getArrayElement(maxVal, 1), 0.0)),
                                          static_cast<float>(getDouble(getArrayElement(maxVal, 2), 0.0)));
                    hasBounds = true;
                }
            }

            std::vector<uint8_t> attrData;
            loadAccessor(getArrayElement(m_accessors, static_cast<uint32_t>(accessorIdx)), attrData);

            if (!attrData.empty()) {
                attributeData.push_back({attribName, std::move(attrData)});
            }
        }
    }

    if (attributeData.empty() || vertexCount == 0) {
        RP_CORE_ERROR("No vertex data found for primitive");
        return false;
    }

    if (hasBounds) {
        node->boundingBoxMin = minBounds;
        node->boundingBoxMax = maxBounds;
        node->hasBoundingBox = true;
    }

    uint32_t vertexStride = 0;
    std::vector<uint32_t> attrSizes;
    std::vector<uint32_t> attrOffsets;

    for (const auto &[name, data] : attributeData) {
        uint32_t attrSize = static_cast<uint32_t>(data.size() / vertexCount);
        attrSizes.push_back(attrSize);
        attrOffsets.push_back(vertexStride);
        vertexStride += attrSize;
    }

    BufferLayout bufferLayout;

    for (uint32_t i = 0; i < attributeData.size(); i++) {
        const auto &[name, data] = attributeData[i];
        yyjson_val *attributesObj = getObjectValue(primitiveJson, "attributes");
        int accessorIdx = getInt(getObjectValue(attributesObj, name.c_str()), 0);
        yyjson_val *accessor = getArrayElement(m_accessors, static_cast<uint32_t>(accessorIdx));

        int componentType = getInt(getObjectValue(accessor, "componentType"), 0);
        const char *type = getString(getObjectValue(accessor, "type"), "");

        bufferLayout.buffer_attribs.push_back(
            {stringToBufferAttributeID(name), static_cast<uint32_t>(componentType), std::string(type), attrOffsets[i]});
    }

    bufferLayout.isInterleaved = true;
    bufferLayout.vertexSize = vertexStride;

    uint32_t totalVertexDataSize = vertexCount * vertexStride;

    std::vector<unsigned char> interleavedData(totalVertexDataSize);

    for (uint32_t v = 0; v < vertexCount; v++) {
        unsigned char *vertexDest = interleavedData.data() + (v * vertexStride);
        for (uint32_t a = 0; a < attributeData.size(); a++) {
            const auto &[name, data] = attributeData[a];
            uint32_t attrSize = attrSizes[a];
            const unsigned char *attrSrc = data.data() + (v * attrSize);
            std::memcpy(vertexDest + attrOffsets[a], attrSrc, attrSize);
        }
    }

    std::vector<unsigned char> indexData;
    uint32_t indexType = 0;
    uint32_t indexCount = 0;

    yyjson_val *indicesVal = getObjectValue(primitiveJson, "indices");
    if (indicesVal && yyjson_is_int(indicesVal)) {
        uint32_t indicesIdx = static_cast<uint32_t>(getInt(indicesVal, 0));

        loadAccessor(getArrayElement(m_accessors, indicesIdx), indexData);

        if (!indexData.empty()) {
            indexType = static_cast<uint32_t>(getInt(getObjectValue(getArrayElement(m_accessors, indicesIdx), "componentType"), 0));
            indexCount = static_cast<uint32_t>(getInt(getObjectValue(getArrayElement(m_accessors, indicesIdx), "count"), 0));
        }
    }

    if (indexData.empty()) {
        RP_CORE_ERROR("glTF2Loader: Vertex data only not supported yet");
        return false;
    }

    AllocatorParams params;
    params.bufferLayout = bufferLayout;
    params.vertexData = interleavedData.data();
    params.vertexDataSize = totalVertexDataSize;
    params.indexData = indexData.data();
    params.indexDataSize = static_cast<uint32_t>(indexData.size());
    params.indexCount = indexCount;
    params.indexType = indexType;

    auto mesh = std::make_unique<Mesh>(params);
    std::string meshAssetName = m_filepath.stem().string() + "_" + node->name;
    node->meshRef = AssetManager::registerVirtualAsset(std::move(mesh), meshAssetName, AssetType::MESH);

    yyjson_val *materialVal = getObjectValue(primitiveJson, "material");
    if (materialVal && yyjson_is_int(materialVal)) {
        node->materialIndex = getInt(materialVal, -1);
        if (node->materialIndex >= 0) {
            loadMaterial(static_cast<size_t>(node->materialIndex));
        }
    }

    parent->children.push_back(std::move(node));
    return true;
}

glm::mat4 glTF2Loader::getNodeTransform(yyjson_val *nodeVal)
{
    glm::mat4 transformMatrix = glm::mat4(1.0f);

    // Extract transform components if present
    yyjson_val *matrixVal = getObjectValue(nodeVal, "matrix");
    if (matrixVal && yyjson_is_arr(matrixVal)) {
        // Use matrix directly
        float matrixValues[16];
        for (int i = 0; i < 16 && i < (int)getArraySize(matrixVal); i++) {
            yyjson_val *element = getArrayElement(matrixVal, i);
            matrixValues[i] = (float)getDouble(element, 0.0);
        }
        transformMatrix = glm::make_mat4(matrixValues);
    } else {
        // Use TRS components
        glm::vec3 translation(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);

        yyjson_val *translationVal = getObjectValue(nodeVal, "translation");
        if (translationVal && yyjson_is_arr(translationVal) && getArraySize(translationVal) >= 3) {
            translation = glm::vec3((float)getDouble(getArrayElement(translationVal, 0), 0.0),
                                    (float)getDouble(getArrayElement(translationVal, 1), 0.0),
                                    (float)getDouble(getArrayElement(translationVal, 2), 0.0));
        }

        yyjson_val *rotationVal = getObjectValue(nodeVal, "rotation");
        if (rotationVal && yyjson_is_arr(rotationVal) && getArraySize(rotationVal) >= 4) {
            // glTF quaternions are [x,y,z,w], but glm::quat constructor takes [w,x,y,z]
            rotation = glm::quat((float)getDouble(getArrayElement(rotationVal, 3), 1.0), // w
                                 (float)getDouble(getArrayElement(rotationVal, 0), 0.0), // x
                                 (float)getDouble(getArrayElement(rotationVal, 1), 0.0), // y
                                 (float)getDouble(getArrayElement(rotationVal, 2), 0.0)  // z
            );
        }

        yyjson_val *scaleVal = getObjectValue(nodeVal, "scale");
        if (scaleVal && yyjson_is_arr(scaleVal) && getArraySize(scaleVal) >= 3) {
            scale =
                glm::vec3((float)getDouble(getArrayElement(scaleVal, 0), 1.0), (float)getDouble(getArrayElement(scaleVal, 1), 1.0),
                          (float)getDouble(getArrayElement(scaleVal, 2), 1.0));
        }

        // Build transform matrix correctly using GLM
        transformMatrix = glm::translate(transformMatrix, translation);
        transformMatrix = transformMatrix * glm::mat4_cast(rotation);
        transformMatrix = glm::scale(transformMatrix, scale);
    }

    return transformMatrix;
}

void glTF2Loader::finalizeToScene(Scene *scene)
{
    if (!scene || !m_loadedData) return;

    std::function<Entity(glTF_SceneNode *, Entity)> createEntityFromNode = [&](glTF_SceneNode *node,
                                                                               Entity parentEntity) -> Entity {
        if (!node) return Entity::null();

        Entity entity = scene->createEntity(node->name);
        entity.addComponent<TransformComponent>(node->worldTransform);

        if (parentEntity.isValid()) {
            setParent(entity, parentEntity);
        }

        if (node->type == glTF_NodeType::PRIMITIVE) {
            if (node->meshRef) {
                entity.addComponent<MeshComponent>(node->meshRef);
                Mesh *mesh = node->meshRef.get()->getUnderlyingAsset<Mesh>();

                if (mesh) {
                    if (node->hasBoundingBox) {
                        entity.addComponent<BoundingBoxComponent>(node->boundingBoxMin, node->boundingBoxMax);
                    }

                    entity.addComponent<BLASComponent>(mesh);
                    scene->registerBLAS(entity);
                }
            }

            if (node->materialIndex >= 0) {
                auto matIt = m_loadedData->materials.find(static_cast<size_t>(node->materialIndex));
                if (matIt != m_loadedData->materials.end() && matIt->second) {
                    entity.addComponent<MaterialComponent>(matIt->second);
                }
            }
        }

        for (auto &child : node->children) {
            createEntityFromNode(child.get(), entity);
        }

        return entity;
    };

    for (auto &rootNode : m_loadedData->rootNodes) {
        createEntityFromNode(rootNode.get(), Entity::null());
    }
}

void glTF2Loader::loadAccessor(yyjson_val *accessorVal, std::vector<unsigned char> &dataVec)
{
    // Clear output vector
    dataVec.clear();

    // Validate accessor has necessary fields
    if (!accessorVal || !yyjson_is_obj(accessorVal) || !getObjectValue(accessorVal, "count") ||
        !getObjectValue(accessorVal, "componentType") || !getObjectValue(accessorVal, "type")) {
        RP_CORE_ERROR("glTF2Loader: Accessor is missing required fields");
        return;
    }

    unsigned int bufferviewInd = getInt(getObjectValue(accessorVal, "bufferView"), 0);
    if (bufferviewInd >= getArraySize(m_bufferViews)) {
        RP_CORE_ERROR("glTF2Loader: Buffer view index out of range: {}", bufferviewInd);
        return;
    }

    unsigned int count = getInt(getObjectValue(accessorVal, "count"), 0);
    unsigned int componentType = getInt(getObjectValue(accessorVal, "componentType"), 0);
    uint32_t accbyteOffset = getInt(getObjectValue(accessorVal, "byteOffset"), 0);
    const char *type = getString(getObjectValue(accessorVal, "type"), "");

    yyjson_val *bufferView = getArrayElement(m_bufferViews, bufferviewInd);
    uint32_t byteOffset = getInt(getObjectValue(bufferView, "byteOffset"), 0) + accbyteOffset;
    uint32_t byteStride = getInt(getObjectValue(bufferView, "byteStride"), 0);

    // Calculate element size
    unsigned int elementSize = 1; // default for SCALAR
    if (strcmp(type, "VEC2") == 0) elementSize = 2;
    else if (strcmp(type, "VEC3") == 0) elementSize = 3;
    else if (strcmp(type, "VEC4") == 0) elementSize = 4;
    else if (strcmp(type, "MAT4") == 0) elementSize = 16;

    unsigned int componentSize = 0;
    switch (componentType) {
    case 5120:
        componentSize = 1;
        break; // BYTE
    case 5121:
        componentSize = 1;
        break; // UNSIGNED_BYTE
    case 5122:
        componentSize = 2;
        break; // SHORT
    case 5123:
        componentSize = 2;
        break; // UNSIGNED_SHORT
    case 5125:
        componentSize = 4;
        break; // UNSIGNED_INT
    case 5126:
        componentSize = 4;
        break; // FLOAT
    default:
        RP_CORE_ERROR("glTF2Loader: Unknown component type: {}", componentType);
        return;
    }

    // Total bytes for this accessor
    unsigned int totalBytes = count * elementSize * componentSize;

    // Pre-allocate the vector to avoid reallocations
    dataVec.reserve(totalBytes);
    dataVec.resize(totalBytes);

    // Check if we need to handle interleaved data with stride
    if (byteStride > 0 && byteStride != (elementSize * componentSize)) {
        // Data is interleaved, need to copy with stride
        unsigned int elementBytes = elementSize * componentSize;
        unsigned char *dstPtr = dataVec.data();

        for (unsigned int i = 0; i < count; i++) {
            if (byteOffset + i * byteStride + elementBytes > m_binVec.size()) {
                RP_CORE_ERROR("glTF2Loader: Buffer access out of bounds");
                dataVec.clear();
                return;
            }

            const unsigned char *srcPtr = m_binVec.data() + byteOffset + i * byteStride;
            std::memcpy(dstPtr, srcPtr, elementBytes);
            dstPtr += elementBytes;
        }
    } else {
        // Data is tightly packed, can copy in one go
        if (byteOffset + totalBytes > m_binVec.size()) {
            RP_CORE_ERROR("glTF2Loader: Buffer access out of bounds: offset={}, size={}, buffer size={}", byteOffset, totalBytes,
                          m_binVec.size());
            return;
        }

        std::memcpy(dataVec.data(), m_binVec.data() + byteOffset, totalBytes);
    }
}

SceneFileMetadata glTF2Loader::getMetadata()
{
    SceneFileMetadata metadata;
    metadata.sourcePath = m_filepath;

    if (!m_glTFroot) {
        return metadata;
    }

    yyjson_val *asset = getObjectValue(m_glTFroot, "asset");
    if (asset) {
        metadata.version = getString(getObjectValue(asset, "version"), "");
        metadata.generator = getString(getObjectValue(asset, "generator"), "");
    }

    metadata.meshCount = getArraySize(m_meshes);
    metadata.materialCount = getArraySize(m_materials);
    metadata.animationCount = getArraySize(m_animations);
    metadata.nodeCount = getArraySize(m_nodes);
    metadata.textureCount = getArraySize(m_textures);
    metadata.hasSkeletons = getArraySize(m_skins) > 0;

    return metadata;
}

AssetRef glTF2Loader::loadMaterial(size_t materialIndex)
{
    auto it = m_loadedData->materials.find(materialIndex);
    if (it != m_loadedData->materials.end()) {
        return it->second;
    }

    if (materialIndex >= getArraySize(m_materials)) {
        return AssetRef();
    }

    yyjson_val *materialVal = getArrayElement(m_materials, static_cast<uint32_t>(materialIndex));
    if (!materialVal) {
        return AssetRef();
    }

    std::string materialName = getString(getObjectValue(materialVal, "name"), "");
    if (materialName.empty()) {
        materialName = "Material_" + std::to_string(materialIndex);
    }

    glm::vec3 baseColor(0.5f, 0.5f, 0.5f);
    float metallic = 0.0f;
    float roughness = 0.5f;

    auto baseMaterial = MaterialManager::getMaterial("PBR");
    auto material = std::make_unique<MaterialInstance>(baseMaterial, materialName);

    yyjson_val *pbrMetallicRoughness = getObjectValue(materialVal, "pbrMetallicRoughness");
    if (pbrMetallicRoughness) {
        yyjson_val *baseColorFactorVal = getObjectValue(pbrMetallicRoughness, "baseColorFactor");
        if (baseColorFactorVal && yyjson_is_arr(baseColorFactorVal) && getArraySize(baseColorFactorVal) >= 3) {
            baseColor = glm::vec3(static_cast<float>(getDouble(getArrayElement(baseColorFactorVal, 0), 0.5)),
                                  static_cast<float>(getDouble(getArrayElement(baseColorFactorVal, 1), 0.5)),
                                  static_cast<float>(getDouble(getArrayElement(baseColorFactorVal, 2), 0.5)));
        }

        yyjson_val *metallicFactorVal = getObjectValue(pbrMetallicRoughness, "metallicFactor");
        if (metallicFactorVal) {
            metallic = static_cast<float>(getDouble(metallicFactorVal, 0.0));
        }

        yyjson_val *roughnessFactorVal = getObjectValue(pbrMetallicRoughness, "roughnessFactor");
        if (roughnessFactorVal) {
            roughness = static_cast<float>(getDouble(roughnessFactorVal, 0.5));
        }

        yyjson_val *baseColorTextureInfo = getObjectValue(pbrMetallicRoughness, "baseColorTexture");
        if (baseColorTextureInfo) {
            int texIndex = getInt(getObjectValue(baseColorTextureInfo, "index"), -1);
            if (texIndex != -1) {
                loadAndSetTexture(material.get(), ParameterID::ALBEDO_MAP, texIndex);
            }
        }

        yyjson_val *metallicRoughnessTextureInfo = getObjectValue(pbrMetallicRoughness, "metallicRoughnessTexture");
        if (metallicRoughnessTextureInfo) {
            int texIndex = getInt(getObjectValue(metallicRoughnessTextureInfo, "index"), -1);
            if (texIndex != -1) {
                loadAndSetTexture(material.get(), ParameterID::METALLIC_ROUGHNESS_MAP, texIndex);
            }
        }
    }

    yyjson_val *normalTextureInfo = getObjectValue(materialVal, "normalTexture");
    if (normalTextureInfo) {
        int texIndex = getInt(getObjectValue(normalTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material.get(), ParameterID::NORMAL_MAP, texIndex);
        }
    }

    yyjson_val *occlusionTextureInfo = getObjectValue(materialVal, "occlusionTexture");
    if (occlusionTextureInfo) {
        int texIndex = getInt(getObjectValue(occlusionTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material.get(), ParameterID::AO_MAP, texIndex);
        }
    }

    yyjson_val *emissiveTextureInfo = getObjectValue(materialVal, "emissiveTexture");
    if (emissiveTextureInfo) {
        int texIndex = getInt(getObjectValue(emissiveTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material.get(), ParameterID::EMISSIVE_MAP, texIndex);
        }
    }

    yyjson_val *emissiveFactorVal = getObjectValue(materialVal, "emissiveFactor");
    if (emissiveFactorVal && yyjson_is_arr(emissiveFactorVal) && getArraySize(emissiveFactorVal) >= 3) {
        glm::vec4 emissiveFactor(static_cast<float>(getDouble(getArrayElement(emissiveFactorVal, 0), 0.0)),
                                 static_cast<float>(getDouble(getArrayElement(emissiveFactorVal, 1), 0.0)),
                                 static_cast<float>(getDouble(getArrayElement(emissiveFactorVal, 2), 0.0)), 1.0f);
        material->setParameter(ParameterID::EMISSIVE, emissiveFactor);
    }

    material->setParameter(ParameterID::ALBEDO, glm::vec4(baseColor, 1.0f));
    material->setParameter(ParameterID::METALLIC, metallic);
    material->setParameter(ParameterID::ROUGHNESS, roughness);

    AssetRef ref = AssetManager::registerVirtualAsset(std::move(material), materialName, AssetType::MATERIAL);
    m_loadedData->materials[materialIndex] = ref;

    return ref;
}

void glTF2Loader::loadSkin(yyjson_val *skinVal)
{
    (void)skinVal;
}

void glTF2Loader::loadWeights(yyjson_val *weightsVal)
{
    (void)weightsVal;
}

void glTF2Loader::loadAnimation(yyjson_val *animationVal)
{
    (void)animationVal;
}

void glTF2Loader::cleanUp()
{
    if (m_glTFdoc != nullptr) {
        yyjson_doc_free(m_glTFdoc);
    }
    m_glTFdoc = nullptr;
    m_glTFroot = nullptr;
    m_accessors = nullptr;
    m_meshes = nullptr;
    m_bufferViews = nullptr;
    m_buffers = nullptr;
    m_nodes = nullptr;
    m_materials = nullptr;
    m_animations = nullptr;
    m_skins = nullptr;
    m_textures = nullptr;
    m_images = nullptr;
    m_samplers = nullptr;

    m_binVec.clear();

    m_isInitialized = false;
    m_isLoaded = false;
}

std::string glTF2Loader::getNodeName(size_t nodeIndex)
{
    if (nodeIndex >= getArraySize(m_nodes)) {
        return std::to_string(nodeIndex);
    }

    yyjson_val *nodeVal = getArrayElement(m_nodes, static_cast<uint32_t>(nodeIndex));
    const char *nodeName = getString(getObjectValue(nodeVal, "name"), "");

    if (strlen(nodeName) == 0) {
        return std::to_string(nodeIndex);
    }

    return nodeName;
}

} // namespace Rapture
