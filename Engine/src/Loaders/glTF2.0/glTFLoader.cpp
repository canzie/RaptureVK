#include "glTFLoader.h"

#include <fstream>
#include <iostream>
#include <type_traits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Components/Components.h"
#include "Components/HierarchyComponent.h"

#include "Buffers/VertexBuffers/BufferLayout.h"

#include "Meshes/Mesh.h"

#include "Materials/MaterialParameters.h"
#include "AssetManager/AssetManager.h"
#include "Events/AssetEvents.h"

namespace Rapture {

bool ModelLoadersCache::s_initialized = false;
std::map<std::filesystem::path, std::weak_ptr<glTF2Loader>> ModelLoadersCache::s_loaders;
std::mutex ModelLoadersCache::s_mutex;

glTF2Loader::glTF2Loader(std::shared_ptr<Scene> scene)
    : m_scene(scene), m_glTFdoc(nullptr), m_glTFroot(nullptr), m_accessors(nullptr), m_meshes(nullptr), m_bufferViews(nullptr),
      m_buffers(nullptr), m_nodes(nullptr), m_materials(nullptr), m_animations(nullptr), m_skins(nullptr), m_textures(nullptr),
      m_images(nullptr), m_samplers(nullptr)
{
    if (!m_scene) {
        RP_CORE_WARN("Scene pointer is null");
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

void glTF2Loader::loadAndSetTexture(std::shared_ptr<MaterialInstance> material, ParameterID id, int textureIndex)
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

    auto [tex, handle] = AssetManager::importAsset<Texture>(texturePathFS, texImportConfig);

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

bool glTF2Loader::initialize(const std::string &filepath)
{
    RP_CORE_INFO("Initializing loader for '{}'", filepath);

    m_isLoaded = false;
    if (m_isInitialized) return true;

    cleanUp();

    // Read the gltf file content into string
    std::ifstream gltf_file(filepath);
    if (!gltf_file) {
        RP_CORE_ERROR("Couldn't load glTF file '{}'", filepath);
        return false;
    }

    m_filepath = filepath;

    // Read entire file into string
    std::string gltf_content((std::istreambuf_iterator<char>(gltf_file)), std::istreambuf_iterator<char>());
    gltf_file.close();

    if (gltf_content.empty()) {
        RP_CORE_ERROR("Empty glTF file '{}'", filepath);
        return false;
    }

    // Parse the JSON using yyjson
    yyjson_read_err err;
    m_glTFdoc = yyjson_read(gltf_content.c_str(), gltf_content.length(), 0);

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

    // Load references to major sections
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

    // Validate required sections
    if (!m_accessors || !m_meshes || !m_bufferViews || !m_buffers) {
        RP_CORE_ERROR("Missing required glTF sections");
        return false;
    }

    // Extract the directory path from the filepath
    m_basePath = "";
    size_t lastSlashPos = filepath.find_last_of("/\\");
    if (lastSlashPos != std::string::npos) {
        m_basePath = filepath.substr(0, lastSlashPos + 1);
    }

    // Load the bin file with all the buffer data
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

    // Check if the buffer URI is a relative path
    std::string fullBufferPath;
    if (strstr(bufferURI, "://") == nullptr && strlen(bufferURI) > 0) {
        // Combine the directory path with the buffer URI
        fullBufferPath = m_basePath + bufferURI;
    } else {
        fullBufferPath = bufferURI;
    }

    std::ifstream binary_file(fullBufferPath, std::ios::binary);
    if (!binary_file) {
        RP_CORE_ERROR("Couldn't load binary file '{}'", fullBufferPath);
        return false;
    }

    // Get file size and reserve space
    binary_file.seekg(0, std::ios::end);
    size_t fileSize = binary_file.tellg();
    binary_file.seekg(0, std::ios::beg);

    m_binVec.resize(fileSize);

    // Read the entire file at once for efficiency
    if (!binary_file.read(reinterpret_cast<char *>(m_binVec.data()), fileSize)) {
        RP_CORE_ERROR("Failed to read binary data");
        return false;
    }

    binary_file.close();

    m_isInitialized = true;
    return true;
}

bool glTF2Loader::loadModel(const std::string &filepath, bool calculateBoundingBoxes)
{
    (void)calculateBoundingBoxes;
    if (m_scene == nullptr) {
        RP_CORE_ERROR("Scene pointer is null");
        return false;
    }

    if (!m_isInitialized || !initialize(filepath)) {
        RP_CORE_ERROR("Failed to initialize loader for '{}'", filepath);
        return false;
    }

    // Create a root entity for the model
    m_scene->createEntity("glTF_Model");

    // Check if the model has animations
    if (m_animations && getArraySize(m_animations) > 0) {
        RP_CORE_INFO("Model has {} animations", getArraySize(m_animations));
    }

    // Process the default scene or the first scene if default not specified
    yyjson_val *sceneIndexVal = getObjectValue(m_glTFroot, "scene");
    int defaultScene = getInt(sceneIndexVal, 0);

    yyjson_val *scenes = getObjectValue(m_glTFroot, "scenes");
    if (scenes && getArraySize(scenes) > 0) {
        yyjson_val *sceneToProcess = getArrayElement(scenes, defaultScene);
        if (sceneToProcess) {
            processScene(sceneToProcess);
        }
    } else if (m_nodes && getArraySize(m_nodes) > 0) {
        // If no scenes but has nodes, process the first node as root
        Entity nodeEntity = m_scene->createEntity("Root Node");
        nodeEntity.addComponent<HierarchyComponent>();

        yyjson_val *firstNode = getArrayElement(m_nodes, 0);
        if (firstNode) {
            processNode(nodeEntity, firstNode);
        }
    }

    // Clean up
    cleanUp();

    m_isLoaded = true;
    return true;
}

void glTF2Loader::processScene(yyjson_val *sceneVal)
{
    // Create a root entity for the scene
    // const char *sceneName = getString(getObjectValue(sceneVal, "name"), "Scene");

    // Process each node in the scene
    yyjson_val *sceneNodes = getObjectValue(sceneVal, "nodes");
    if (sceneNodes && yyjson_is_arr(sceneNodes)) {
        size_t nodeCount = getArraySize(sceneNodes);
        for (size_t i = 0; i < nodeCount; i++) {
            yyjson_val *nodeIndexVal = getArrayElement(sceneNodes, i);
            unsigned int nodeIndex = getInt(nodeIndexVal, 0);
            if (nodeIndex < getArraySize(m_nodes)) {
                Entity nodeEntity = m_scene->createEntity("Node " + std::to_string(nodeIndex));
                nodeEntity.addComponent<HierarchyComponent>();
                yyjson_val *nodeToProcess = getArrayElement(m_nodes, nodeIndex);
                if (nodeToProcess) {
                    processNode(nodeEntity, nodeToProcess);
                }
            }
        }
    }
}

std::shared_ptr<MaterialInstance> glTF2Loader::processMaterial(Entity parentEntity, yyjson_val *materialVal)
{
    (void)parentEntity;

    // Get material name if available
    std::string materialName = getString(getObjectValue(materialVal, "name"), "");

    // Extract PBR parameters from material JSON
    glm::vec3 baseColor(0.5f, 0.5f, 0.5f); // Default base color
    float metallic = 0.0f;                 // Default metallic
    float roughness = 0.5f;                // Default roughness

    // Create a PBR material using the MaterialLibrary
    auto baseMaterial = MaterialManager::getMaterial("PBR");
    std::shared_ptr<MaterialInstance> material = std::make_shared<MaterialInstance>(baseMaterial, materialName);

    // If no specular-glossiness extension, process standard metallic-roughness
    yyjson_val *pbrMetallicRoughness = getObjectValue(materialVal, "pbrMetallicRoughness");
    if (pbrMetallicRoughness) {

        // Base color factor
        yyjson_val *baseColorFactorVal = getObjectValue(pbrMetallicRoughness, "baseColorFactor");
        if (baseColorFactorVal && yyjson_is_arr(baseColorFactorVal) && getArraySize(baseColorFactorVal) >= 3) {
            baseColor = glm::vec3((float)getDouble(getArrayElement(baseColorFactorVal, 0), 0.5),
                                  (float)getDouble(getArrayElement(baseColorFactorVal, 1), 0.5),
                                  (float)getDouble(getArrayElement(baseColorFactorVal, 2), 0.5));
        }

        // Metallic factor
        yyjson_val *metallicFactorVal = getObjectValue(pbrMetallicRoughness, "metallicFactor");
        if (metallicFactorVal) {
            metallic = (float)getDouble(metallicFactorVal, 0.0);
        }

        // Roughness factor
        yyjson_val *roughnessFactorVal = getObjectValue(pbrMetallicRoughness, "roughnessFactor");
        if (roughnessFactorVal) {
            roughness = (float)getDouble(roughnessFactorVal, 0.5);
        }

        // Load textures
        // Base color texture
        yyjson_val *baseColorTextureInfo = getObjectValue(pbrMetallicRoughness, "baseColorTexture");
        if (baseColorTextureInfo) {
            int texIndex = getInt(getObjectValue(baseColorTextureInfo, "index"), -1);
            if (texIndex != -1) {
                loadAndSetTexture(material, ParameterID::ALBEDO_MAP, texIndex);
            }
        }

        // Metallic roughness texture
        yyjson_val *metallicRoughnessTextureInfo = getObjectValue(pbrMetallicRoughness, "metallicRoughnessTexture");
        if (metallicRoughnessTextureInfo) {
            int texIndex = getInt(getObjectValue(metallicRoughnessTextureInfo, "index"), -1);
            if (texIndex != -1) {
                loadAndSetTexture(material, ParameterID::METALLIC_ROUGHNESS_MAP, texIndex);
            }
        }
    }

    // Normal map - common to both workflows
    yyjson_val *normalTextureInfo = getObjectValue(materialVal, "normalTexture");
    if (normalTextureInfo) {
        int texIndex = getInt(getObjectValue(normalTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material, ParameterID::NORMAL_MAP, texIndex);
        }
    }

    // Occlusion map - common to both workflows
    yyjson_val *occlusionTextureInfo = getObjectValue(materialVal, "occlusionTexture");
    if (occlusionTextureInfo) {
        int texIndex = getInt(getObjectValue(occlusionTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material, ParameterID::AO_MAP, texIndex);
        }
    }

    // Emissive map - common to both workflows
    yyjson_val *emissiveTextureInfo = getObjectValue(materialVal, "emissiveTexture");
    if (emissiveTextureInfo) {
        int texIndex = getInt(getObjectValue(emissiveTextureInfo, "index"), -1);
        if (texIndex != -1) {
            loadAndSetTexture(material, ParameterID::EMISSIVE_MAP, texIndex);
        }
    }

    // Emissive factor - common to both workflows
    yyjson_val *emissiveFactorVal = getObjectValue(materialVal, "emissiveFactor");
    if (emissiveFactorVal && yyjson_is_arr(emissiveFactorVal) && getArraySize(emissiveFactorVal) >= 3) {
        glm::vec4 emissiveFactor((float)getDouble(getArrayElement(emissiveFactorVal, 0), 0.0),
                                 (float)getDouble(getArrayElement(emissiveFactorVal, 1), 0.0),
                                 (float)getDouble(getArrayElement(emissiveFactorVal, 2), 0.0),
                                 1.0f); // strength
        material->setParameter(ParameterID::EMISSIVE, emissiveFactor);
    }

    // dont need to update the descriptor set here, as the ubo is already good, only the textures need to be added to the descriptor
    // set
    material->setParameter(ParameterID::ALBEDO, glm::vec4(baseColor, 1.0f));
    material->setParameter(ParameterID::METALLIC, metallic);
    material->setParameter(ParameterID::ROUGHNESS, roughness);

    // material->updateDescriptorSet();

    return material;
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

NodeType glTF2Loader::processNode(Entity nodeEntity, yyjson_val *nodeVal)
{
    if (!nodeEntity.hasComponent<HierarchyComponent>()) {
        return NodeType::Empty;
    }

    const char *nodeName = getString(getObjectValue(nodeVal, "name"), "");
    // Entity nodeEntity = m_scene->createEntity(nodeName);

    // Update the tag
    if (nodeName && strlen(nodeName) > 0) {
        nodeEntity.getComponent<TagComponent>().tag = std::string(nodeName);
    }
    auto &nodeEntityComp = nodeEntity.getComponent<HierarchyComponent>();

    auto &transformComp = nodeEntity.addComponent<TransformComponent>();

    glm::mat4 nodeTransform = getNodeTransform(nodeVal);

    Entity parent = nodeEntityComp.parent;
    if (parent.isValid()) {
        nodeTransform = parent.getComponent<TransformComponent>().transformMatrix() * nodeTransform;
    }

    transformComp.transforms.setTransform(nodeTransform);

    // If this node has a mesh, process it
    yyjson_val *meshVal = getObjectValue(nodeVal, "mesh");
    if (meshVal && yyjson_is_int(meshVal)) {
        unsigned int meshIndex = getInt(meshVal, 0);
        if (meshIndex < getArraySize(m_meshes)) {
            processMesh(nodeEntity, getArrayElement(m_meshes, meshIndex));
        }
    }

    bool hasMeshChild = false;
    // Process children
    yyjson_val *childrenVal = getObjectValue(nodeVal, "children");
    if (childrenVal && yyjson_is_arr(childrenVal)) {
        size_t childCount = getArraySize(childrenVal);
        for (size_t i = 0; i < childCount; i++) {
            yyjson_val *childIndexVal = getArrayElement(childrenVal, i);
            unsigned int childIndex = getInt(childIndexVal, 0);
            if (childIndex < getArraySize(m_nodes)) {
                Entity childEntity = m_scene->createEntity("Node " + std::to_string(childIndex));
                childEntity.addComponent<HierarchyComponent>(nodeEntity);

                nodeEntity.getComponent<HierarchyComponent>().addChild(childEntity);

                NodeType nodeType = processNode(childEntity, getArrayElement(m_nodes, childIndex));

                if (nodeType == NodeType::Bone) {
                    // use the child transform as the bone transform

                    // then remove the entity, as it is a bone and we just need the transform
                    m_scene->destroyEntity(childEntity);

                    // child is either a mesh, or an empty node which has a mesh somewhere as its child
                    //    if the leaf node was not a mesh, it would be a bone type
                    //    and propagated up the tree until a mesh was found, then it will always be a mesh or empty type
                } else if (nodeType == NodeType::Mesh || nodeType == NodeType::Empty) {
                    hasMeshChild = true;
                }
            }
        }
    }

    // we are the mesh
    if (meshVal && yyjson_is_int(meshVal)) {
        return NodeType::Mesh;

        // descendants have a mesh
    } else if (hasMeshChild) {
        return NodeType::Empty;

        // we have a skeleton
    } else if (getObjectValue(nodeVal, "skin")) {
        return NodeType::Skeleton;

        // we are a bone
    } else {
        return NodeType::Bone;
    }
}

Entity glTF2Loader::processMesh(Entity parent, yyjson_val *meshVal)
{
    auto &parentTransform = parent.getComponent<TransformComponent>();

    const char *meshName = getString(getObjectValue(meshVal, "name"), "Mesh");
    Entity meshEntity = m_scene->createEntity(meshName);
    // Create transform component that inherits parent transform
    meshEntity.addComponent<TransformComponent>(parentTransform.transformMatrix());

    // Check if parent entity has HierarchyComponent
    if (!parent.hasComponent<HierarchyComponent>()) {
        RP_CORE_ERROR("Parent entity '{}' missing HierarchyComponent", meshName);
        return meshEntity;
    }

    meshEntity.addComponent<HierarchyComponent>(parent);
    parent.getComponent<HierarchyComponent>().addChild(meshEntity);

    // Process primitives
    yyjson_val *primitivesVal = getObjectValue(meshVal, "primitives");
    if (primitivesVal && yyjson_is_arr(primitivesVal)) {
        size_t primitiveCount = getArraySize(primitivesVal);
        for (size_t i = 0; i < primitiveCount; i++) {
            // For each primitive, create a new entity
            Entity primitiveEntity = m_scene->createEntity("_Primitive_" + std::to_string(i) + "_" + meshName);
            // RP_CORE_INFO("glTF2Loader: Mesh loaded: {}", meshName);

            primitiveEntity.addComponent<HierarchyComponent>(meshEntity);
            meshEntity.getComponent<HierarchyComponent>().addChild(primitiveEntity);

            primitiveEntity.addComponent<TransformComponent>(parentTransform.transformMatrix());

            // Process the primitive data
            processPrimitive(primitiveEntity, getArrayElement(primitivesVal, i));
        }
    }

    return meshEntity;
}

void glTF2Loader::processPrimitive(Entity entity, yyjson_val *primitiveVal)
{
    // Add mesh component to the entity
    auto &meshComp = entity.addComponent<MeshComponent>();

    // Gather attribute data and calculate attribute sizes
    std::vector<std::pair<std::string, std::vector<unsigned char>>> attributeData;

    std::vector<unsigned char> temp_indexData;

    unsigned int vertexCount = 0;

    glm::vec3 minBounds(std::numeric_limits<float>::infinity());
    glm::vec3 maxBounds(-std::numeric_limits<float>::infinity());
    bool calculatedBounds = false;

    // Process vertex attributes
    yyjson_val *attributesVal = getObjectValue(primitiveVal, "attributes");
    if (attributesVal && yyjson_is_obj(attributesVal)) {

        // First pass: gather data and determine vertex count
        yyjson_obj_iter iter = yyjson_obj_iter_with(attributesVal);
        yyjson_val *key, *val;
        while ((key = yyjson_obj_iter_next(&iter))) {
            val = yyjson_obj_iter_get_val(key);
            const char *attribName = yyjson_get_str(key);
            if (strcmp(attribName, "COLOR_0") == 0) continue; // Skip color data for now

            unsigned int accessorIdx = getInt(val, 0);
            yyjson_val *accessor = getArrayElement(m_accessors, accessorIdx);

            if (strcmp(attribName, "POSITION") == 0 && accessor) {
                yyjson_val *minVal = getObjectValue(accessor, "min");
                yyjson_val *maxVal = getObjectValue(accessor, "max");
                if (minVal && yyjson_is_arr(minVal) && getArraySize(minVal) >= 3 && maxVal && yyjson_is_arr(maxVal) &&
                    getArraySize(maxVal) >= 3) {
                    minBounds = glm::vec3((float)getDouble(getArrayElement(minVal, 0), 0.0),
                                          (float)getDouble(getArrayElement(minVal, 1), 0.0),
                                          (float)getDouble(getArrayElement(minVal, 2), 0.0));
                    maxBounds = glm::vec3((float)getDouble(getArrayElement(maxVal, 0), 0.0),
                                          (float)getDouble(getArrayElement(maxVal, 1), 0.0),
                                          (float)getDouble(getArrayElement(maxVal, 2), 0.0));
                    calculatedBounds = true;
                }
            }

            // Get vertex count from the first attribute (should be the same for all attributes)
            if (vertexCount == 0 && accessor) {
                vertexCount = getInt(getObjectValue(accessor, "count"), 0);
            }

            // Load attribute data
            std::vector<unsigned char> attrData;
            loadAccessor(getArrayElement(m_accessors, accessorIdx), attrData);

            if (!attrData.empty()) {
                attributeData.push_back({attribName, attrData});
            }
        }
    }

    // Early exit if no vertex data
    if (attributeData.empty() || vertexCount == 0) {
        RP_CORE_ERROR("No vertex data found for primitive");
        return;
    }

    // Calculate attribute sizes and strides
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

    // Create buffer layout for interleaved data

    for (uint32_t i = 0; i < attributeData.size(); i++) {
        const auto &[name, data] = attributeData[i];
        yyjson_val *attributesObj = getObjectValue(primitiveVal, "attributes");
        int accessorIdx = getInt(getObjectValue(attributesObj, name.c_str()), 0);
        yyjson_val *accessor = getArrayElement(m_accessors, accessorIdx);

        int componentType = getInt(getObjectValue(accessor, "componentType"), 0);
        const char *type = getString(getObjectValue(accessor, "type"), "");

        // For interleaved data, the offset is the relative position within a single vertex
        try {
            bufferLayout.buffer_attribs.push_back(
                {stringToBufferAttributeID(name), static_cast<uint32_t>(componentType), std::string(type), attrOffsets[i]});
        } catch (const std::runtime_error &e) {
            RP_CORE_ERROR("{}", e.what());
        }

        // Find position attribute and record its offset
        if (name == "POSITION") {

            if (entity.hasComponent<BoundingBoxComponent>() && calculatedBounds) {
                entity.getComponent<BoundingBoxComponent>().localBoundingBox = BoundingBox(minBounds, maxBounds);
            } else if (calculatedBounds) {
                entity.addComponent<BoundingBoxComponent>(minBounds, maxBounds);
                // entity.addComponent<Entropy::RigidBodyComponent>(std::make_unique<Entropy::AABBCollider>(minBounds, maxBounds));
            }
        }
    }

    // Set interleaved flag to true
    bufferLayout.isInterleaved = true;
    bufferLayout.vertexSize = vertexStride;

    // Create vertex buffer with the correct size
    uint32_t totalVertexDataSize = vertexCount * vertexStride;

    // Pre-allocate interleaved data with the exact known size
    std::vector<unsigned char> interleavedData;
    interleavedData.reserve(totalVertexDataSize);
    interleavedData.resize(totalVertexDataSize);

    // Fill the interleaved buffer using direct memory access
    for (uint32_t v = 0; v < vertexCount; v++) {
        unsigned char *vertexDest = interleavedData.data() + (v * vertexStride);
        for (uint32_t a = 0; a < attributeData.size(); a++) {
            const auto &[name, data] = attributeData[a];
            uint32_t attrSize = attrSizes[a];
            const unsigned char *attrSrc = data.data() + (v * attrSize);
            unsigned char *attrDest = vertexDest + attrOffsets[a];

            // Direct memory copy
            std::memcpy(attrDest, attrSrc, attrSize);
        }
    }

    // Process indices if present
    std::vector<unsigned char> indexData;
    unsigned int compType = 0;
    unsigned int indCount = 0;

    yyjson_val *indicesVal = getObjectValue(primitiveVal, "indices");
    if (indicesVal && yyjson_is_int(indicesVal)) {
        unsigned int indicesIdx = getInt(indicesVal, 0);

        // Pre-allocate index data to avoid reallocation
        indexData.reserve(getInt(getObjectValue(getArrayElement(m_accessors, indicesIdx), "count"), 0) *
                          4); // Worst case: 4 bytes per index

        // Load index data
        loadAccessor(getArrayElement(m_accessors, indicesIdx), indexData);

        if (!indexData.empty()) {
            // Get index component type
            compType = getInt(getObjectValue(getArrayElement(m_accessors, indicesIdx), "componentType"), 0);
            indCount = getInt(getObjectValue(getArrayElement(m_accessors, indicesIdx), "count"), 0);
        }
    }

    {
        if (indexData.size() > 0) {
            AllocatorParams params;
            params.bufferLayout = bufferLayout;
            params.vertexData = (void *)interleavedData.data();
            params.vertexDataSize = totalVertexDataSize;
            params.indexData = (void *)indexData.data();
            params.indexDataSize = static_cast<uint32_t>(indexData.size());
            params.indexCount = indCount;
            params.indexType = compType;

            meshComp.mesh->setMeshData(params);
        } else {
            RP_CORE_ERROR("glTF2Loader: Vertex data only not supported yet");
            entity.removeComponent<MeshComponent>();
            return;
        }
    }

    // Process material if present
    yyjson_val *materialVal = getObjectValue(primitiveVal, "material");
    if (materialVal) {
        uint32_t materialIdx = getInt(materialVal, 0);
        if (materialIdx < getArraySize(m_materials)) {
            yyjson_val *materialData = getArrayElement(m_materials, materialIdx);
            if (materialData) {
                auto material = processMaterial(entity, materialData);

                if (material && entity.hasComponent<MaterialComponent>()) {
                    // Add material component to the entity
                    entity.getComponent<MaterialComponent>().material = material;
                } else {
                    entity.addComponent<MaterialComponent>(material);
                }
            }
        }
    }
    try {
        entity.addComponent<BLASComponent>(meshComp.mesh);

        m_scene->registerBLAS(entity);
    } catch (const std::runtime_error &e) {
        RP_CORE_ERROR("glTF2Loader: Failed to add BLAS component: {}", e.what());
    }

    // Mark the mesh as loaded
    meshComp.isLoading = false;
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

    // Clean up the cache when a loader is cleaned up
    // ONLY call this when initialized already, otherwise we can end up in a deadlock->crash
    if (m_isInitialized) {
        ModelLoadersCache::cleanup();
    }

    m_isInitialized = false;
    m_isLoaded = false;
}

std::string glTF2Loader::getNodeName(unsigned int nodeIndex)
{
    if (nodeIndex >= getArraySize(m_nodes)) {
        return std::to_string(nodeIndex);
    }

    yyjson_val *nodeVal = getArrayElement(m_nodes, nodeIndex);
    const char *nodeName = getString(getObjectValue(nodeVal, "name"), "");

    if (strlen(nodeName) == 0) {
        return std::to_string(nodeIndex);
    }

    return nodeName;
}

} // namespace Rapture