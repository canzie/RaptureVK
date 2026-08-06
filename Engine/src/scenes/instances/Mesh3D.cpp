#include "Mesh3D.h"

#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "logging/Log.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_MESH = "mesh";
static constexpr std::string_view KEY_MATERIAL = "material";
static constexpr std::string_view KEY_VISIBLE = "visible";
static constexpr std::string_view KEY_MOBILITY = "mobility";
static constexpr std::string_view KEY_RAY_TRACED = "rayTraced";

Mesh3D::Mesh3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.setComponent<MeshComponent>();
    m_entity.setComponent<MaterialComponent>();
}

const TypeInfo &Mesh3D::staticType()
{
    static const TypeInfo type("Mesh3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Mesh3D::type() const
{
    return staticType();
}

AssetHandle Mesh3D::mesh() const
{
    const auto *component = m_entity.tryGetComponent<MeshComponent>();
    if (component == nullptr || !component->mesh) {
        return INVALID_ASSET_HANDLE;
    }

    return component->mesh.ref().get()->getHandle();
}

void Mesh3D::setMesh(AssetHandle mesh)
{
    AssetRef ref = AssetManager::getAsset(mesh);
    if (!ref) {
        RP_CORE_ERROR("mesh {} could not be resolved for '{}'", mesh, name());
        return;
    }

    // Assigned in place because replacing the component would drop its render data slot, which is
    // only handed out by the construction signal.
    auto *component = m_entity.tryGetComponent<MeshComponent>();
    if (component == nullptr) {
        return;
    }

    component->setMesh(std::move(ref));
    component->isLoading = false;
    m_entity.markDirty();
}

AssetHandle Mesh3D::material() const
{
    const auto *component = m_entity.tryGetComponent<MaterialComponent>();
    if (component == nullptr || !component->material) {
        return INVALID_ASSET_HANDLE;
    }

    return component->material.ref().get()->getHandle();
}

void Mesh3D::setMaterial(AssetHandle material)
{
    AssetRef ref = AssetManager::getAsset(material);
    if (!ref) {
        RP_CORE_ERROR("material {} could not be resolved for '{}'", material, name());
        return;
    }

    auto *component = m_entity.tryGetComponent<MaterialComponent>();
    if (component == nullptr) {
        return;
    }

    component->material = AssetPtr<MaterialInstance>(std::move(ref));
    m_entity.markDirty();
}

bool Mesh3D::isVisible() const
{
    const auto *component = m_entity.tryGetComponent<MeshComponent>();
    return component != nullptr ? component->isEnabled : false;
}

void Mesh3D::setVisible(bool visible)
{
    auto *component = m_entity.tryGetComponent<MeshComponent>();
    if (component == nullptr) {
        return;
    }

    component->isEnabled = visible;
    m_entity.markDirty();
}

Mobility Mesh3D::mobility() const
{
    const auto *component = m_entity.tryGetComponent<MeshComponent>();
    return component != nullptr ? component->mobility : MOBILITY_STATIC;
}

void Mesh3D::setMobility(Mobility mobility)
{
    if (!m_entity.hasComponent<MeshComponent>()) {
        return;
    }

    scene()->getRenderData()->setMeshMobility(m_entity.getID(), mobility);
    m_entity.markDirty();
}

glm::vec3 Mesh3D::boundsMin() const
{
    const auto *component = m_entity.tryGetComponent<MeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->getBoundsMin() : glm::vec3(0.0f);
}

glm::vec3 Mesh3D::boundsMax() const
{
    const auto *component = m_entity.tryGetComponent<MeshComponent>();
    return (component != nullptr && component->mesh) ? component->mesh->getBoundsMax() : glm::vec3(0.0f);
}

bool Mesh3D::isRayTraced() const
{
    return m_entity.hasComponent<RayTracedComponent>();
}

void Mesh3D::setRayTraced(bool rayTraced)
{
    if (!rayTraced) {
        m_entity.tryRemoveComponent<RayTracedComponent>();
        return;
    }

    if (m_entity.hasComponent<RayTracedComponent>()) {
        return;
    }

    auto *component = m_entity.tryGetComponent<MeshComponent>();
    if (component == nullptr || !component->mesh) {
        RP_CORE_ERROR("'{}' cannot be ray traced before it has a mesh", name());
        return;
    }

    if (!component->mesh->buildBLAS()) {
        RP_CORE_ERROR("'{}' cannot be ray traced because its mesh has no acceleration structure", name());
        return;
    }

    m_entity.addComponent<RayTracedComponent>();

    Entity self = m_entity;
    scene()->registerBLAS(self);
}

void Mesh3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode mesh = node.addObject(KEY_MESH);
    mesh.set(KEY_MESH, this->mesh());
    mesh.set(KEY_MATERIAL, material());
    mesh.set(KEY_VISIBLE, isVisible());
    mesh.set(KEY_MOBILITY, static_cast<uint64_t>(mobility()));
    mesh.set(KEY_RAY_TRACED, isRayTraced());
}

void Mesh3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode mesh = node.child(KEY_MESH);
    if (!mesh.valid()) {
        return;
    }

    setMobility(static_cast<Mobility>(mesh.child(KEY_MOBILITY).asU64(static_cast<uint64_t>(mobility()))));

    AssetHandle meshHandle = mesh.child(KEY_MESH).asU64(INVALID_ASSET_HANDLE);
    if (meshHandle != INVALID_ASSET_HANDLE) {
        setMesh(meshHandle);
    }

    AssetHandle materialHandle = mesh.child(KEY_MATERIAL).asU64(INVALID_ASSET_HANDLE);
    if (materialHandle != INVALID_ASSET_HANDLE) {
        setMaterial(materialHandle);
    }

    setVisible(mesh.child(KEY_VISIBLE).asBool(isVisible()));
    setRayTraced(mesh.child(KEY_RAY_TRACED).asBool(isRayTraced()));
}

} // namespace Rapture
