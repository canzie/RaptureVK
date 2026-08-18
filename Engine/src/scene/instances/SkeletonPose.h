#ifndef RAPTURE__SKELETON_POSE_H
#define RAPTURE__SKELETON_POSE_H

#include "assets/asset_manager/AssetCommon.h"
#include "scene/instances/SceneObject.h"
#include "scene/render_data/SkeletonInstanceManager.h"

#include <memory>

namespace Rapture {

/**
 * @brief A skeleton being posed, which any number of skeletal meshes are drawn against.
 */
class SkeletonPose : public SceneObject {
  public:
    SkeletonPose(Scene &scene, std::string_view name, AssetHandle skeleton);
    SkeletonPose(Scene &scene, std::string_view name);
    ~SkeletonPose() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle skeleton() const { return m_skeleton; }

    /**
     * @brief The bones this pose is drawn from
     * @return The bones, or nullptr if the skeleton could not be resolved
     */
    SkeletonInstance *skeletonInstance() const { return m_skeletonInstance.get(); }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    /**
     * @brief Resolves the skeleton and takes room for its bones, dropping any skeleton already posed
     * @param skeleton The skeleton to pose
     */
    void bindSkeleton(AssetHandle skeleton);

  private:
    AssetHandle m_skeleton = INVALID_ASSET_HANDLE;
    std::unique_ptr<SkeletonInstance> m_skeletonInstance;
};

} // namespace Rapture

#endif // RAPTURE__SKELETON_POSE_H
