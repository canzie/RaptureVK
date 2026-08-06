#ifndef RAPTURE__SCENE_ASSET_H
#define RAPTURE__SCENE_ASSET_H

#include "serialization/SerialDocument.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Rapture {

class Scene;

/**
 * @brief The stored form of a scene, holding the document a live scene is built from.
 */
class SceneAsset {
  public:
    /**
     * @brief Builds an asset holding a live scene's current contents
     * @param scene The scene to capture
     */
    explicit SceneAsset(const Scene &scene);

    /**
     * @brief Cursor to the scene the document holds, valid while this asset lives
     * @return The root cursor, invalid if the document could not be read
     */
    ReadNode root() const { return m_document.rootView(); }

    /**
     * @brief Serializes this asset into a self-contained blob
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const;

    /**
     * @brief Rebuilds a scene asset from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The asset, or nullptr if the blob does not hold a readable document
     */
    static std::unique_ptr<SceneAsset> deserialize(std::span<const uint8_t> blob);

  private:
    SceneAsset() = default;

    SerialDocument m_document;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_ASSET_H
