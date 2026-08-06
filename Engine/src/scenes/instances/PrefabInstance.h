#ifndef RAPTURE__PREFAB_INSTANCE_H
#define RAPTURE__PREFAB_INSTANCE_H

#include "asset_manager/AssetCommon.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief The root of an instantiated prefab, holding the blueprint its subtree came from.
 */
class PrefabInstance : public Node3D {
  public:
    PrefabInstance(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief The blueprint this subtree was instantiated from
     * @return The prefab handle, or INVALID_ASSET_HANDLE if it has none
     */
    AssetHandle prefab() const;

    /**
     * @brief Points this root at the blueprint it came from
     * @param prefab Handle of the prefab asset
     */
    void setPrefab(AssetHandle prefab);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__PREFAB_INSTANCE_H
