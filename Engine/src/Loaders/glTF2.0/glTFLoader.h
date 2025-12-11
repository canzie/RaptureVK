#pragma once

#include "yyjson.h"
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Logging/Log.h"
#include "Materials/MaterialInstance.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"

namespace Rapture {
// Forward declaration
// class ModelAssetCache;

/**
 * @brief Structure to hold glTF file metadata
 */
struct glTFMetadata {
    size_t materialCount = 0;
    size_t primitiveCount = 0;
    size_t animationCount = 0;
    size_t nodeCount = 0;
    size_t meshCount = 0;
    size_t textureCount = 0;
    bool hasSkeletons = false;
    std::string version;
    std::string generator;
};

enum class NodeType {
    Empty,
    Mesh,
    Bone,
    Skeleton,
};

/**
 * @brief Modern loader for glTF 2.0 format 3D models using entity-component architecture
 *
 * This class handles loading glTF model files and creating entities with appropriate components.
 * Each glTF node becomes an entity with transform and mesh components as needed.
 */
class glTF2Loader {
  public:
    /**
     * @brief Constructor that takes a scene to populate
     *
     * @param scene Pointer to the scene where entities will be created
     */
    glTF2Loader(std::shared_ptr<Scene> scene);

    /**
     * @brief Destructor
     */
    ~glTF2Loader();

    /**
     * @brief Static method to quickly analyze a glTF file and return metadata
     *
     * @param filepath Path to the .gltf file
     * @param isAbsolute If true, filepath is an absolute path
     * @return glTFMetadata structure containing file information
     */
    static glTFMetadata getFileMetadata(const std::string &filepath, bool isAbsolute = false);

    bool initialize(const std::string &filepath);

    /**
     * @brief Load a model from a glTF file and populate the scene with entities
     *
     * @param filepath Path to the .gltf file
     * @param calculateBoundingBoxes If true, bounding boxes will be calculated for all primitives
     * @return true if loading was successful, false otherwise
     */
    bool loadModel(const std::string &filepath, bool calculateBoundingBoxes = false);

    /**
     * @brief Check if file was successfully loaded
     *
     * @return true if file is loaded and ready
     */
    bool isLoaded() const { return m_isLoaded; }

    // Friend declaration for the cache
    friend class ModelAssetCache;

  private:
    /**
     * @brief Process a glTF primitive and set up mesh data
     *
     * @param entity Entity to attach mesh data to
     * @param primitive yyjson value containing primitive data
     */
    void processPrimitive(Entity entity, yyjson_val *primitive);

    /**
     * @brief Extract raw binary data from an accessor
     *
     * @param accessorVal yyjson value containing accessor information
     * @param data_vec Vector to store the extracted binary data
     */
    void loadAccessor(yyjson_val *accessorVal, std::vector<unsigned char> &data_vec);

    /**
     * @brief Process a mesh from the glTF file and create entities
     *
     * @param parentEntity Parent entity for this mesh
     * @param meshVal yyjson value containing mesh data
     * @return Entity The created entity
     */
    Entity processMesh(Entity parentEntity, yyjson_val *meshVal);

    /**
     * @brief Process the node hierarchy and create entities with proper transforms
     *
     * @param parentEntity Parent entity
     * @param nodeVal yyjson value containing node data
     * @return Entity The created entity
     */
    NodeType processNode(Entity parentEntity, yyjson_val *nodeVal);

    /**
     * @brief Process a scene from the glTF file
     *
     * @param sceneVal yyjson value containing scene data
     * @return Entity The root scene entity
     */
    void processScene(yyjson_val *sceneVal);

    std::shared_ptr<MaterialInstance> processMaterial(Entity parentEntity, yyjson_val *materialVal);

    /**
     * @brief Clean up all data after loading
     */
    void cleanUp();

    /**
     * @brief Get the transform matrix of a node
     *
     * @param nodeVal yyjson value containing node data
     * @return The transform matrix of the node
     */
    glm::mat4 getNodeTransform(yyjson_val *nodeVal);

    /**
     * @brief Get node name from glTF node index
     *
     * @param nodeIndex Index of the node in glTF file
     * @return Name of the node
     */
    std::string getNodeName(unsigned int nodeIndex);

    // Helper functions for yyjson
    yyjson_val *getObjectValue(yyjson_val *obj, const char *key);
    yyjson_val *getArrayElement(yyjson_val *arr, uint32_t index);
    const char *getString(yyjson_val *val, const char *defaultValue = "");
    int getInt(yyjson_val *val, int defaultValue = 0);
    double getDouble(yyjson_val *val, double defaultValue = 0.0);
    bool getBool(yyjson_val *val, bool defaultValue = false);
    size_t getArraySize(yyjson_val *arr);

    void loadAndSetTexture(std::shared_ptr<MaterialInstance> material, ParameterID id, int texIndex);

  private:
    // Reference to the scene being populated
    std::shared_ptr<Scene> m_scene;

    // JSON document and main sections from the glTF file
    yyjson_doc *m_glTFdoc;
    yyjson_val *m_glTFroot;
    yyjson_val *m_accessors;
    yyjson_val *m_meshes;
    yyjson_val *m_bufferViews;
    yyjson_val *m_buffers;
    yyjson_val *m_nodes;
    yyjson_val *m_materials;
    yyjson_val *m_animations;
    yyjson_val *m_skins;
    yyjson_val *m_textures;
    yyjson_val *m_images;
    yyjson_val *m_samplers;

    bool m_calculateBoundingBoxes = false;

    // Raw binary data from the .bin file
    std::vector<unsigned char> m_binVec;

    // Base path for loading external resources
    std::string m_basePath;

    // Constants for glTF component types
    static const unsigned int GLTF_FLOAT = 5126;
    static const unsigned int GLTF_UINT = 5125;
    static const unsigned int GLTF_USHORT = 5123;
    static const unsigned int GLTF_SHORT = 5122;
    static const unsigned int GLTF_UBYTE = 5121;
    static const unsigned int GLTF_BYTE = 5120;

    bool m_isLoaded = false;
    bool m_isInitialized = false;
    std::string m_filepath;
};

/**
 * @brief Cache for loaded model assets to avoid redundant file operations
 * @note This caching system only works because a reference to the loader will be used in the loader itself...
 *       The loader will call importasset, the import asset will then get the loader which called the importasset...
 *       This way the loader will not get expired atleast until the original loader shared pointer goes out of scope.
 *       very goofy, so watch out for weird bugs.
 *       (asssumtion) one cenario which will negate the benefits is if the original caller of the loader does not get its loader
 * trough the modelassetcache and it does not keep a shared pointer to the loader (return value of getLoader).
 */
class ModelLoadersCache {
  public:
    /**
     * @brief Get a loader for a specific model file
     *
     * @param filepath Path to the model file
     * @param isAbsolute If true, filepath is an absolute path
     * @return std::shared_ptr<glTF2Loader> A loader instance for the file
     */

    static void init()
    {
        if (s_initialized) return;
        s_loaders.clear();
        s_initialized = true;
    }

    static std::shared_ptr<glTF2Loader> getLoader(const std::filesystem::path &filepath, std::shared_ptr<Scene> scene = nullptr)
    {
        if (!s_initialized) {
            RP_CORE_ERROR("ModelLoadersCache - Not initialized");
            return nullptr;
        }

        if (s_loaders.find(filepath) != s_loaders.end()) {
            if (auto loader = s_loaders[filepath].lock()) {
                return loader;
            } else {
                RP_CORE_WARN("ModelLoadersCache - Loader for '{}' expired, removing from cache", filepath.string());
                s_loaders.erase(filepath);
            }
        }

        auto loader = std::make_shared<glTF2Loader>(scene);

        {

            std::lock_guard<std::mutex> lock(s_mutex);

            if (!loader->initialize(filepath.string())) {
                RP_CORE_ERROR("ModelLoadersCache::getLoader - Failed to initialize loader for '{}'", filepath.string());
                return nullptr;
            }
            s_loaders[filepath] = loader;
        }

        return loader;
    }

    /**
     * @brief Clear cache entries not used recently
     */
    static void cleanup()
    {
        if (!s_initialized) return;

        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto it = s_loaders.begin(); it != s_loaders.end();) {
            if (it->second.expired()) {
                // RP_CORE_INFO("ModelLoadersCache::cleanup - Loader for '{}' expired, removing from cache", it->first); //
                // Commented out for stability
                it = s_loaders.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Clear all cached loaders
     */
    static void clear()
    {
        if (!s_initialized) return;

        std::lock_guard<std::mutex> lock(s_mutex);
        s_loaders.clear();
    }

    friend class glTF2Loader;

  private:
    // Maps the filepath to the loader
    static bool s_initialized;
    static std::map<std::filesystem::path, std::weak_ptr<glTF2Loader>> s_loaders;
    static std::mutex s_mutex;
};
} // namespace Rapture
