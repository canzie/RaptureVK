#include "Mesh3D.h"
#include "assets/materials/AMaterialInstance.h"

#include "assets/asset_manager/AssetManager.h"
#include "assets/meshes/AMesh.h"
#include "core/utils/Log.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

static constexpr std::string_view KEY_MESH = "mesh";
static constexpr std::string_view KEY_MATERIALS = "materials";
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

AssetHandle Mesh3D::material(uint32_t slot) const
{
    if (slot >= m_materials.size()) {
        RP_CORE_WARN("slot {} is not one of the {} '{}' draws", slot, m_materials.size(), name());
        return INVALID_ASSET_HANDLE;
    }
    return m_materials[slot];
}

void Mesh3D::setMaterial(uint32_t slot, AssetHandle _material)
{
    if (slot >= m_materials.size()) {
        RP_CORE_WARN("slot {} is not one of the {} '{}' draws, leaving its materials as they were", slot, m_materials.size(),
                     name());
        return;
    }

    if (m_materials[slot] == _material) {
        return;
    }

    Ref<AMaterialInstance> ref = AssetManager::getAsset<AMaterialInstance>(_material);
    if (!ref) {
        RP_CORE_ERROR("material {} could not be resolved for '{}'", _material, name());
        return;
    }

    if (!m_entity.has<MaterialComponent>()) {
        return;
    }
    auto component = m_entity.write<MaterialComponent>();

    component->materials.resize(m_materials.size());
    component->materials[slot] = std::move(ref);
    m_materials[slot] = _material;
}

void Mesh3D::adoptMaterialSlots(const AMesh &mesh)
{
    const std::vector<AssetHandle> &slots = mesh.materialSlots();

    // a run this object already draws keeps what it was given, the rest take what the mesh names
    std::vector<AssetHandle> adopted = slots;
    for (size_t slot = 0; slot < adopted.size() && slot < m_materials.size(); slot++) {
        if (m_materials[slot] != INVALID_ASSET_HANDLE) {
            adopted[slot] = m_materials[slot];
        }
    }

    m_materials.assign(adopted.size(), INVALID_ASSET_HANDLE);
    for (uint32_t slot = 0; slot < adopted.size(); slot++) {
        setMaterial(slot, adopted[slot]);
    }
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

    WriteNode materials = mesh.addArray(KEY_MATERIALS);
    for (AssetHandle handle : m_materials) {
        materials.append(handle);
    }

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

    ReadNode materials = mesh.child(KEY_MATERIALS);
    for (uint32_t slot = 0; slot < materials.size(); slot++) {
        AssetHandle materialHandle = materials.at(slot).asU64(INVALID_ASSET_HANDLE);
        if (materialHandle != INVALID_ASSET_HANDLE) {
            setMaterial(slot, materialHandle);
        }
    }

    setVisible(mesh.child(KEY_VISIBLE).asBool(isVisible()));
    setRayTraced(mesh.child(KEY_RAY_TRACED).asBool(isRayTraced()));
}

} // namespace Rapture
