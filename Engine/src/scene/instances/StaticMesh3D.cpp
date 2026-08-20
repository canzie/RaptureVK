#include "StaticMesh3D.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

StaticMesh3D::StaticMesh3D(Scene &scene, std::string_view name) : Mesh3D(scene, name)
{
    m_entity.set<StaticMeshComponent>();
}

const TypeInfo &StaticMesh3D::staticType()
{
    static const TypeInfo type("StaticMesh3D", &Mesh3D::staticType());
    return type;
}

const TypeInfo &StaticMesh3D::type() const
{
    return staticType();
}

void StaticMesh3D::setMesh(AssetHandle _mesh)
{
    if (m_mesh == _mesh) {
        return;
    }

    AssetRef ref = AssetManager::getAsset(_mesh);
    if (!ref) {
        RP_CORE_ERROR("mesh {} could not be resolved for '{}'", _mesh, name());
        return;
    }

    if (!m_entity.has<StaticMeshComponent>()) {
        return;
    }

    const AStaticMesh *asset = ref.get()->getUnderlyingAsset<AStaticMesh>();

    {
        auto component = m_entity.write<StaticMeshComponent>();

        component->setMesh(std::move(ref));
        component->isLoading = false;
    }
    m_mesh = _mesh;

    if (asset != nullptr) {
        adoptDefaultMaterial(*asset);
    }

    if (m_entity.has<RayTracedComponent>()) {
        rebuildAccelerationStructure();
    }
}

void StaticMesh3D::rebuildAccelerationStructure()
{
    const StaticMeshComponent *component = m_entity.tryRead<StaticMeshComponent>();
    if (component == nullptr || !component->mesh || !component->mesh->geometry().buildBLAS()) {
        RP_CORE_ERROR("'{}' left ray tracing because its mesh has no acceleration structure", name());
        m_entity.tryRemove<RayTracedComponent>();
        scene()->unregisterBLAS(m_entity.getEntity());
        return;
    }

    scene()->registerBLAS(m_entity.getEntity());
}

bool StaticMesh3D::isVisible() const
{
    const auto *component = m_entity.tryRead<StaticMeshComponent>();
    return component != nullptr ? component->isEnabled : false;
}

void StaticMesh3D::setVisible(bool visible)
{
    if (!m_entity.has<StaticMeshComponent>()) {
        return;
    }
    auto component = m_entity.write<StaticMeshComponent>();

    component->isEnabled = visible;
}

Mobility StaticMesh3D::mobility() const
{
    const auto *component = m_entity.tryRead<StaticMeshComponent>();
    return component != nullptr ? component->mobility : MOBILITY_STATIC;
}

void StaticMesh3D::setMobility(Mobility mobility)
{
    if (!m_entity.has<StaticMeshComponent>()) {
        return;
    }

    scene()->getRenderData()->setMeshMobility(m_entity.getEntity(), mobility);
}

glm::vec3 StaticMesh3D::boundsMin() const
{
    const auto *component = m_entity.tryRead<StaticMeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->geometry().getBoundsMin() : glm::vec3(0.0f);
}

glm::vec3 StaticMesh3D::boundsMax() const
{
    const auto *component = m_entity.tryRead<StaticMeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->geometry().getBoundsMax() : glm::vec3(0.0f);
}

void StaticMesh3D::setRayTraced(bool rayTraced)
{
    if (!rayTraced) {
        if (m_entity.tryRemove<RayTracedComponent>()) {
            scene()->unregisterBLAS(m_entity.getEntity());
        }
        return;
    }

    if (m_entity.has<RayTracedComponent>()) {
        return;
    }

    const StaticMeshComponent *component = m_entity.tryRead<StaticMeshComponent>();
    if (component == nullptr || !component->mesh) {
        RP_CORE_ERROR("'{}' cannot be ray traced before it has a mesh", name());
        return;
    }

    if (!component->mesh->geometry().buildBLAS()) {
        RP_CORE_ERROR("'{}' cannot be ray traced because its mesh has no acceleration structure", name());
        return;
    }

    m_entity.add<RayTracedComponent>();

    scene()->registerBLAS(m_entity.getEntity());
}

} // namespace Rapture
