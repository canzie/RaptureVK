#include "SkeletalMesh3D.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

static constexpr std::string_view KEY_SKELETAL = "skeletal";
static constexpr std::string_view KEY_POSE = "pose";

SkeletalMesh3D::SkeletalMesh3D(Scene &scene, std::string_view name) : Mesh3D(scene, name)
{
    m_entity.set<SkeletalMeshComponent>();
}

const TypeInfo &SkeletalMesh3D::staticType()
{
    static const TypeInfo type("SkeletalMesh3D", &Mesh3D::staticType());
    return type;
}

const TypeInfo &SkeletalMesh3D::type() const
{
    return staticType();
}

void SkeletalMesh3D::setMesh(AssetHandle _mesh)
{
    if (m_mesh == _mesh) {
        return;
    }

    Ref<ASkeletalMesh> ref = AssetManager::getAsset<ASkeletalMesh>(_mesh);
    if (!ref) {
        RP_CORE_ERROR("mesh {} could not be resolved for '{}'", _mesh, name());
        return;
    }

    if (!m_entity.has<SkeletalMeshComponent>()) {
        return;
    }

    {
        auto component = m_entity.write<SkeletalMeshComponent>();

        component->setMesh(ref);
        component->isLoading = false;
        m_mesh = _mesh;
    }

    adoptMaterialSlots(*ref);

    writePose();
}

void SkeletalMesh3D::setPose(SkeletonPose *pose)
{
    m_pose.set(pose);
    writePose();
}

void SkeletalMesh3D::writePose()
{
    if (!m_entity.has<SkeletalMeshComponent>()) {
        return;
    }

    auto component = m_entity.write<SkeletalMeshComponent>();

    component->pose = m_pose ? m_pose->skeletonInstance() : nullptr;
    component->inverseBindOffset = SKIN_NO_OFFSET;

    if (component->pose == nullptr || !component->mesh) {
        return;
    }

    const uint32_t jointCount = component->mesh->geometry().getJointCount();
    if (jointCount != m_pose->getJointCount()) {
        RP_CORE_ERROR("'{}' is bound to {} joints and cannot be deformed by a pose of {}", name(), jointCount,
                      m_pose->getJointCount());
        component->pose = nullptr;
        return;
    }

    SkeletonInstanceManager &manager = scene()->getRenderData()->getSkeletonInstanceManager();
    component->inverseBindOffset = manager.getInverseBindOffset(component->mesh);
}

bool SkeletalMesh3D::isVisible() const
{
    const auto *component = m_entity.tryRead<SkeletalMeshComponent>();
    return component != nullptr ? component->isEnabled : false;
}

void SkeletalMesh3D::setVisible(bool visible)
{
    if (!m_entity.has<SkeletalMeshComponent>()) {
        return;
    }
    auto component = m_entity.write<SkeletalMeshComponent>();

    component->isEnabled = visible;
}

Mobility SkeletalMesh3D::mobility() const
{
    return MOBILITY_DYNAMIC;
}

void SkeletalMesh3D::setMobility(Mobility mobility)
{
    (void)mobility;
}

glm::vec3 SkeletalMesh3D::boundsMin() const
{
    const auto *component = m_entity.tryRead<SkeletalMeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->geometry().getBoundsMin() : glm::vec3(0.0f);
}

glm::vec3 SkeletalMesh3D::boundsMax() const
{
    const auto *component = m_entity.tryRead<SkeletalMeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->geometry().getBoundsMax() : glm::vec3(0.0f);
}

void SkeletalMesh3D::setRayTraced(bool rayTraced)
{
    if (!rayTraced) {
        return;
    }

    RP_CORE_ERROR("'{}' cannot be ray traced, a skinned mesh has no acceleration structure to trace", name());
}

void SkeletalMesh3D::serialize(WriteNode node) const
{
    Mesh3D::serialize(node);

    WriteNode skeletal = node.addObject(KEY_SKELETAL);
    m_pose.serialize(skeletal, KEY_POSE);
}

void SkeletalMesh3D::deserialize(ReadNode node)
{
    Mesh3D::deserialize(node);

    ReadNode skeletal = node.child(KEY_SKELETAL);
    if (!skeletal.valid()) {
        return;
    }

    m_pose.deserialize(skeletal, KEY_POSE);
}

void SkeletalMesh3D::onLink(const SceneLoadContext &context)
{
    m_pose.link(context);
}

void SkeletalMesh3D::onReady()
{
    writePose();
}

} // namespace Rapture
