#ifndef RAPTURE__ASKELETON_H
#define RAPTURE__ASKELETON_H

#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetCommon.h"
#include "assets/skeletons/Skeleton.h"
#include "core/events/EventSignal.h"

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief A skeleton asset: the joint hierarchy, and the meshes it is shown on.
 *
 * A skeleton draws nothing of its own, so seeing one means borrowing geometry bound to it. The
 * meshes it is shown on are seeded at import and authored from there.
 */
class ASkeleton : public Asset {
  public:
    explicit ASkeleton(std::unique_ptr<Skeleton> skeleton, std::vector<AssetHandle> previewMeshes = {});

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    Skeleton &skeleton() { return *m_skeleton; }
    const Skeleton &skeleton() const { return *m_skeleton; }

    /**
     * @brief The meshes this skeleton is shown on where it has to be seen
     * @return The mesh assets, which are not resolved until something shows them
     */
    std::span<const AssetHandle> previewMeshes() const { return m_previewMeshes; }

    /**
     * @brief Adds a mesh to show this skeleton on, rejecting one bound to another skeleton
     * @param mesh The skeletal mesh asset to add
     * @return True if the mesh was added
     */
    bool addPreviewMesh(AssetHandle mesh);

    /**
     * @brief Stops showing this skeleton on a mesh
     * @param mesh The skeletal mesh asset to drop
     * @return True if the mesh was being shown
     */
    bool removePreviewMesh(AssetHandle mesh);

    /**
     * @brief Serializes this asset into a self contained blob
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const override;

    /**
     * @brief Builds a skeleton asset from a blob produced by serialize
     * @param blob The serialized bytes
     * @return The asset, or nullptr if the blob is not a readable skeleton
     */
    static std::unique_ptr<ASkeleton> deserialize(std::span<const uint8_t> blob);

    /**
     * @brief Fires after a mesh is added to or dropped from the set this skeleton is shown on
     */
    EventSignal<void()> onPreviewMeshesChanged;

  private:
    std::unique_ptr<Skeleton> m_skeleton;
    std::vector<AssetHandle> m_previewMeshes;
};

} // namespace Rapture

#endif // RAPTURE__ASKELETON_H
