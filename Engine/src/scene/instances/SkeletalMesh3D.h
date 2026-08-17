#ifndef RAPTURE__SKELETALMESH3D_H
#define RAPTURE__SKELETALMESH3D_H

#include "scene/instances/InstanceRef.h"
#include "scene/instances/Mesh3D.h"
#include "scene/instances/SkeletonPose.h"

namespace Rapture {

/**
 * @brief A mesh drawn deformed by a skeleton pose.
 */
class SkeletalMesh3D : public Mesh3D {
  public:
    SkeletalMesh3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void setMesh(AssetHandle mesh) override;

    bool isVisible() const override;
    void setVisible(bool visible) override;

    Mobility mobility() const override;
    void setMobility(Mobility mobility) override;

    glm::vec3 boundsMin() const override;
    glm::vec3 boundsMax() const override;

    void setRayTraced(bool rayTraced) override;

    SkeletonPose *pose() const { return m_pose.get(); }

    /**
     * @brief Draws this mesh against another pose
     * @param pose The pose to be deformed by, or nullptr to leave it in its bind pose
     */
    void setPose(SkeletonPose *pose);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void onLink(const SceneLoadContext &context) override;
    void onReady() override;

  private:
    /**
     * @brief Puts the pose this mesh is drawn against into the storage the draw reads it from
     */
    void writePose();

  private:
    InstanceRef<SkeletonPose> m_pose;
};

} // namespace Rapture

#endif // RAPTURE__SKELETALMESH3D_H
