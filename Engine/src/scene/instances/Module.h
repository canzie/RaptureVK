#ifndef RAPTURE__MODULE_H
#define RAPTURE__MODULE_H

#include "assets/asset_manager/AssetCommon.h"
#include "scene/instances/Node3D.h"

namespace Rapture {

/**
 * @brief A module asset standing in a scene, with what the asset describes held below it.
 */
class Module : public Node3D {
  public:
    Module(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Points this object at a module asset and reads what it describes in below itself
     * @param handle The module asset to stand for
     * @return True if the asset was read
     */
    bool setAssetHandle(AssetHandle handle);

    AssetHandle assetHandle() const { return m_assetHandle; }

    /**
     * @brief The object the module asset was written from
     * @return The root of what was read, or nullptr if this object stands for nothing
     */
    SceneObject *contentRoot() const { return m_contentRoot; }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    AssetHandle m_assetHandle = INVALID_ASSET_HANDLE;
    SceneObject *m_contentRoot = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__MODULE_H
