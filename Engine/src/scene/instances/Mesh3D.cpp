#include "Mesh3D.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

static constexpr std::string_view KEY_MESH = "mesh";
static constexpr std::string_view KEY_MATERIAL = "material";
static constexpr std::string_view KEY_VISIBLE = "visible";
static constexpr std::string_view KEY_MOBILITY = "mobility";
static constexpr std::string_view KEY_RAY_TRACED = "rayTraced";

Mesh3D::Mesh3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.set<MaterialComponent>();
}

Mesh3D::~Mesh3D() = default;

const TypeInfo &Mesh3D::staticType()
{
    static const TypeInfo type("Mesh3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Mesh3D::type() const
{
    return staticType();
}

void Mesh3D::setMaterial(AssetHandle _material)
{
    if (m_material == _material) {
        return;
    }

    AssetRef ref = AssetManager::getAsset(_material);
    if (!ref) {
        RP_CORE_ERROR("material {} could not be resolved for '{}'", _material, name());
        return;
    }

    if (!m_entity.has<MaterialComponent>()) {
        return;
    }
    auto component = m_entity.write<MaterialComponent>();

    component->material = AssetPtr<MaterialInstance>(std::move(ref));
    m_material = _material;
}

bool Mesh3D::isRayTraced() const
{
    return m_entity.has<RayTracedComponent>();
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
