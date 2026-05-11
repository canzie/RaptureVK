#ifndef RAPTURE__GLTF_LOADER_H
#define RAPTURE__GLTF_LOADER_H

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
#include <vector>

namespace Rapture {

struct Counter;
class Scene;
class MaterialInstance;
enum class ParameterID;

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
     */
    explicit glTF2Loader(const std::filesystem::path &filepath);
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
    bool loadMesh(glTF_SceneNode *node, size_t meshIndex);
    bool loadPrimitive(glTF_SceneNode *parent, yyjson_val *primitiveJson, size_t primitiveIndex);

    void loadSkin(yyjson_val *skinVal);
    void loadWeights(yyjson_val *weightsVal);
    void loadAnimation(yyjson_val *animationVal);

    AssetRef loadMaterial(size_t materialIndex);

    void finalizeToScene(Scene *scene);

    void loadAccessor(yyjson_val *accessorVal, std::vector<unsigned char> &dataVec);
    void cleanUp();

    glm::mat4 getNodeTransform(yyjson_val *nodeVal);
    std::string getNodeName(size_t nodeIndex);

    yyjson_val *getObjectValue(yyjson_val *obj, const char *key);
    yyjson_val *getArrayElement(yyjson_val *arr, uint32_t index);
    const char *getString(yyjson_val *val, const char *defaultValue = "");
    int getInt(yyjson_val *val, int defaultValue = 0);
    double getDouble(yyjson_val *val, double defaultValue = 0.0);
    bool getBool(yyjson_val *val, bool defaultValue = false);
    size_t getArraySize(yyjson_val *arr);

    void loadAndSetTexture(MaterialInstance *material, ParameterID id, int texIndex);

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

    std::vector<unsigned char> m_binVec;
    std::filesystem::path m_filepath;
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
