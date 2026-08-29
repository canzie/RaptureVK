#ifndef RAPTURE__GLTF_LOADER_H
#define RAPTURE__GLTF_LOADER_H

#include "assets/asset_manager/Asset.h"
#include "assets/materials/MaterialParameters.h"
#include "assets/skeletons/ASkeleton.h"
#include "glTFCommon.h"
#include "yyjson.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

struct Counter;
class MaterialInstance;
class Node3D;
class Scene;
class SceneObject;
class SkeletalMesh3D;

/**
 * @brief Loader for glTF 2.0 format 3D models
 *
 * Parses glTF files into an intermediate scene graph (glTF_LoadedSceneData).
 * If a scene is provided to load(), finalizes to ECS entities after loading.
 */
class glTF2Loader {
  public:
    /**
     * @brief Constructor
     * @param filepath Path to the .gltf file
     * @param outputFolder Directory every asset this loader creates is written into
     * @param name Name for the produced prefab, falls back to the file stem when empty
     */
    explicit glTF2Loader(const std::filesystem::path &filepath, std::filesystem::path outputFolder, std::string name = {});
    ~glTF2Loader();

    /**
     * @brief Load the glTF file and build scene graph
     * @param scene Optional scene for finalization (nullptr to skip ECS creation)
     * @param sceneIndex glTF scene index to load (-1 for default)
     * @return true if loading was successful
     */
    bool load(Scene *scene = nullptr, int32_t sceneIndex = -1);

    /**
     * @brief Get metadata without full load
     */
    SceneFileMetadata getMetadata();

    const glTF_LoadedSceneData *getLoadedData() const { return m_loadedData.get(); }
    bool isLoaded() const { return m_isLoaded; }

  private:
    bool loadScene(yyjson_val *sceneRoot);
    bool loadNode(glTF_SceneNode *parent, size_t nodeIndex);
    /**
     * @brief Builds one glTF mesh into the single mesh asset a node draws
     *
     * The mesh's primitives are merged, one run per material, so a file's mesh is a mesh here too
     * however many primitives it was authored in.
     *
     * @param node The node that draws the mesh
     * @param meshIndex The glTF mesh to build
     * @param skeleton The skeleton this node's skin names, or INVALID_ASSET_HANDLE if it has none
     * @param skin The skin this node is bound through, whose bind pose its primitives are authored in
     * @return True if the mesh was built
     */
    bool loadMesh(glTF_SceneNode *node, size_t meshIndex, AssetHandle skeleton, size_t skin);

    /**
     * @brief Reads one primitive's attributes, indices and material, leaving them unmerged
     * @param primitiveJson The primitive to read
     * @param skeleton The skeleton the mesh is bound to, or INVALID_ASSET_HANDLE if it is not skinned
     * @param out Filled in from the primitive
     * @return True if the primitive holds geometry
     */
    bool decodePrimitive(yyjson_val *primitiveJson, AssetHandle skeleton, glTF_DecodedPrimitive &out);

    /**
     * @brief Builds the one mesh asset a glTF mesh's primitives describe between them
     *
     * The primitives are concatenated into a single pair of buffers, grouped so that everything
     * drawn with one material forms one run and the mesh carries a slot per distinct material.
     *
     * @param primitives The decoded primitives of one glTF mesh
     * @param meshIndex The glTF mesh they came from, which names the asset
     * @param skeleton The skeleton the mesh is bound to, or INVALID_ASSET_HANDLE if it is not skinned
     * @param skin The skin the mesh is bound through, whose bind pose it is authored in
     * @return The imported asset, or null if the primitives hold nothing drawable
     */
    AssetRef mergePrimitives(std::vector<glTF_DecodedPrimitive> &primitives, size_t meshIndex, AssetHandle skeleton, size_t skin);

    /**
     * @brief Builds the scene objects one glTF node subtree describes
     * @param parent The object the new object is parented to
     * @param src The glTF node to build from
     * @return The object built for this node
     */
    Node3D *buildSceneObject(SceneObject &parent, glTF_SceneNode *src);

    /**
     * @brief Adds one pose per skin below an asset's root and points its skinned meshes at it
     * @param root The object the poses are added to
     */
    void buildSkeletonPoses(SceneObject &root);

    /**
     * @brief Imports this file's nodes as a module, and spawns them if there is a scene
     *
     * The objects are built in a scene of their own and serialized from there, so the asset is
     * written by the same classes that read it back.
     *
     * @param scene The scene to spawn the imported objects into, or nullptr to only write the asset
     */
    void buildModule(Scene *scene);

    void loadSkin(yyjson_val *skinVal);
    void loadWeights(yyjson_val *weightsVal);
    void loadAnimation(yyjson_val *animationVal);

    AssetRef loadMaterial(size_t materialIndex);

    /**
     * @brief The material asset a primitive's material index names
     * @param materialIndex Index into the file's materials, negative where the primitive names none
     * @return The material, or the default material where the index names nothing loaded
     */
    AssetHandle primitiveMaterial(int32_t materialIndex) const;

    void loadAccessor(yyjson_val *accessorVal, std::vector<uint8_t> &dataVec);
    void cleanUp();

    glm::mat4 getNodeTransform(yyjson_val *nodeVal);

    /**
     * @brief Reads a node's local transform as translation, rotation and scale
     * @param nodeVal The node to read
     * @return The transform, decomposed from the node's matrix if it has one
     */
    Skeleton::JointTransform getNodeRestTransform(yyjson_val *nodeVal);

    /**
     * @brief Maps each node that has a parent to it, over the whole node array
     * @return Node index to its parent's node index
     */
    std::unordered_map<size_t, size_t> buildNodeParents();

    std::string getNodeName(size_t nodeIndex);

    yyjson_val *getObjectValue(yyjson_val *obj, const char *key);
    yyjson_val *getArrayElement(yyjson_val *arr, uint32_t index);
    const char *getString(yyjson_val *val, const char *defaultValue = "");
    int getInt(yyjson_val *val, int defaultValue = 0);
    double getDouble(yyjson_val *val, double defaultValue = 0.0);
    bool getBool(yyjson_val *val, bool defaultValue = false);
    size_t getArraySize(yyjson_val *arr);

    void loadAndSetTexture(MaterialInstance *material, const ParameterId &id, int texIndex);

  private:
    std::unique_ptr<glTF_LoadedSceneData> m_loadedData;

    yyjson_doc *m_glTFdoc = nullptr;
    yyjson_val *m_glTFroot = nullptr;
    yyjson_val *m_accessors = nullptr;
    yyjson_val *m_meshes = nullptr;
    yyjson_val *m_bufferViews = nullptr;
    yyjson_val *m_buffers = nullptr;
    yyjson_val *m_nodes = nullptr;
    yyjson_val *m_materials = nullptr;
    yyjson_val *m_animations = nullptr;
    yyjson_val *m_skins = nullptr;
    yyjson_val *m_textures = nullptr;
    yyjson_val *m_images = nullptr;
    yyjson_val *m_samplers = nullptr;

    /// glTF mesh index -> the merged asset built for it, so nodes sharing a mesh share one asset
    std::unordered_map<size_t, AssetRef> m_meshCache;

    /// skeleton asset -> the bind pose each skinned mesh under it is bound against
    // keyed by skin, since a bind pose belongs to the binding of one mesh to a skeleton rather than
    // to the skeleton, which two skins may share while standing in different poses
    std::unordered_map<size_t, std::vector<glm::mat4>> m_inverseBindMatrices;
    // ordered, so a reimport names the poses of a file with several skins the same way every time
    std::map<AssetHandle, std::vector<SkeletalMesh3D *>> m_skinnedMeshes;

    std::vector<uint8_t> m_binVec;
    std::filesystem::path m_filepath;
    std::filesystem::path m_outputFolder;
    std::string m_name;
    std::string m_basePath;

    bool m_isLoaded = false;
    bool m_isInitialized = false;

    static constexpr unsigned int GLTF_FLOAT = 5126;
    static constexpr unsigned int GLTF_UINT = 5125;
    static constexpr unsigned int GLTF_USHORT = 5123;
    static constexpr unsigned int GLTF_SHORT = 5122;
    static constexpr unsigned int GLTF_UBYTE = 5121;
    static constexpr unsigned int GLTF_BYTE = 5120;
};

} // namespace Rapture

#endif // RAPTURE__GLTF_LOADER_H
