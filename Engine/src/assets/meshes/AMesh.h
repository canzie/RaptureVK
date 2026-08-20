#ifndef RAPTURE__AMESH_H
#define RAPTURE__AMESH_H

#include "assets/asset_manager/AssetCommon.h"
#include "assets/asset_manager/ReservedAssets.h"

namespace Rapture {

/**
 * @brief A mesh as it is stored and referenced, pairing geometry with what draws it.
 *
 * The geometry below this is vertex and index data alone, and knows nothing of materials. What a
 * mesh is drawn with is a fact about the pairing, which is what this layer is.
 */
class AMesh {
  public:
    virtual ~AMesh() = default;

    AMesh(const AMesh &) = delete;
    AMesh &operator=(const AMesh &) = delete;

    /**
     * @brief The material this mesh is drawn with where nothing else is chosen
     * @return The material
     */
    AssetHandle defaultMaterial() const { return m_defaultMaterial; }

    /**
     * @brief Sets the material this mesh is drawn with where nothing else is chosen
     * @param material The material to default to
     */
    void setDefaultMaterial(AssetHandle material);

  protected:
    explicit AMesh(AssetHandle defaultMaterial) : m_defaultMaterial(defaultMaterial) {}

  private:
    AssetHandle m_defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE;
};

} // namespace Rapture

#endif // RAPTURE__AMESH_H
