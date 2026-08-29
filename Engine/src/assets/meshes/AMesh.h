#ifndef RAPTURE__AMESH_H
#define RAPTURE__AMESH_H

#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetCommon.h"
#include "assets/asset_manager/ReservedAssets.h"

#include <vector>

namespace Rapture {

/**
 * @brief A mesh as it is stored and referenced, pairing geometry with what draws it.
 *
 * The geometry below this is vertex and index data alone, cut into runs but knowing nothing of what
 * fills them. Which material a run is drawn with is a fact about the pairing, which is what this
 * layer is: one slot per run of the geometry, in the same order.
 */
class AMesh : public Asset {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief The materials this mesh's runs are drawn with where nothing else is chosen
     * @return One material per run of the geometry, in the same order
     */
    const std::vector<AssetHandle> &materialSlots() const { return m_materialSlots; }

    /**
     * @brief The material a slot is drawn with where nothing else is chosen
     * @param slot The slot to read
     * @return The material, or the default material if this mesh has no such slot
     */
    AssetHandle materialSlot(uint32_t slot) const;

    /**
     * @brief Sets the material a slot is drawn with where nothing else is chosen
     * @param slot The slot to set, which has to be one this mesh has
     * @param material The material to default to
     */
    void setMaterialSlot(uint32_t slot, AssetHandle material);

  protected:
    explicit AMesh(std::vector<AssetHandle> materialSlots);

  private:
    std::vector<AssetHandle> m_materialSlots;
};

} // namespace Rapture

#endif // RAPTURE__AMESH_H
