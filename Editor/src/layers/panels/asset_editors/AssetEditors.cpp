#include "AssetEditors.h"

#include <assets/asset_manager/AssetManager.h>
#include <assets/meshes/AMesh.h>
#include <assets/meshes/ASkeletalMesh.h>
#include <assets/meshes/AStaticMesh.h>
#include <assets/skeletons/ASkeleton.h>
#include <core/utils/Log.h>

#include <cstdio>
#include <span>
#include <vector>

template <typename T>
static T *s_assetAs(Rapture::AssetHandle handle)
{
    Rapture::AssetRef ref = Rapture::AssetManager::getAsset(handle);
    return ref ? ref->as<T>() : nullptr;
}

/**
 * @brief The mesh an open asset holds, whichever kind of mesh asset it is
 * @param handle The asset to resolve
 * @return The mesh asset, or nullptr if the handle names neither kind
 */
static Rapture::AMesh *s_meshAsset(Rapture::AssetHandle handle)
{
    if (auto *staticMesh = s_assetAs<Rapture::AStaticMesh>(handle)) {
        return staticMesh;
    }
    return s_assetAs<Rapture::ASkeletalMesh>(handle);
}

/**
 * @brief The geometry an open mesh asset holds, whichever kind of mesh asset it is
 * @param handle The asset to resolve
 * @return The geometry, or nullptr if the handle names neither kind
 */
static const Rapture::Mesh *s_meshGeometry(Rapture::AssetHandle handle)
{
    if (auto *staticMesh = s_assetAs<Rapture::AStaticMesh>(handle)) {
        return &staticMesh->geometry();
    }
    if (auto *skeletalMesh = s_assetAs<Rapture::ASkeletalMesh>(handle)) {
        return &skeletalMesh->geometry();
    }
    return nullptr;
}

static std::string s_formatVec3(const glm::vec3 &value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", value.x, value.y, value.z);
    return buffer;
}

void MeshAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_indexCount = rowText(t, "Indices", "0");
        m_boundsMin = rowText(t, "Bounds Min", "");
        m_boundsMax = rowText(t, "Bounds Max", "");
    });
}

void MeshAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::Mesh *mesh = s_meshGeometry(handle);
    if (mesh == nullptr) {
        return;
    }

    if (m_indexCount != nullptr) {
        m_indexCount->setText(std::to_string(mesh->getIndexCount()));
    }
    if (m_boundsMin != nullptr) {
        m_boundsMin->setText(s_formatVec3(mesh->getBoundsMin()));
    }
    if (m_boundsMax != nullptr) {
        m_boundsMax->setText(s_formatVec3(mesh->getBoundsMax()));
    }
}

void MeshMaterialAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowAssetPicker(t, "Default", m_materialPicker, {.types = {Rapture::ASSET_MATERIAL_INSTANCE}},
                       [this](Rapture::AssetHandle material) { applyDefaultMaterial(material); });
    });
}

void MeshMaterialAssetEditor::applyDefaultMaterial(Rapture::AssetHandle material)
{
    Rapture::AMesh *mesh = s_meshAsset(m_handle);
    if (mesh == nullptr) {
        return;
    }

    mesh->setDefaultMaterial(material);

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_handle);
    if (!Rapture::AssetManager::saveAsset(m_handle, metadata.assetPath.parent_path())) {
        RP_ERROR("could not write '{}' back, its default material is only set for this run", metadata.getName());
    }
}

void MeshMaterialAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::AMesh *mesh = s_meshAsset(handle);
    if (mesh == nullptr || !m_materialPicker) {
        return;
    }

    m_materialPicker->setAsset(mesh->defaultMaterial());
}

void MeshSkeletonAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_skeletonName = rowText(t, "Bound To", "");
        m_jointCount = rowText(t, "Joints", "0");
    });
}

void MeshSkeletonAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::ASkeletalMesh *mesh = s_assetAs<Rapture::ASkeletalMesh>(handle);
    if (mesh == nullptr) {
        return;
    }

    if (m_skeletonName != nullptr) {
        m_skeletonName->setText(Rapture::AssetManager::getAssetMetadata(mesh->skeleton()).getName());
    }
    if (m_jointCount != nullptr) {
        m_jointCount->setText(std::to_string(mesh->geometry().getJointCount()));
    }
}

// A mesh names the skeleton it is bound to only once it is loaded, so offering the meshes that fit a
// skeleton means loading each candidate. Filtering without loading needs the binding on the metadata.
static bool s_meshUsesSkeleton(Rapture::AssetHandle mesh, Rapture::AssetHandle skeleton)
{
    const Rapture::ASkeletalMesh *asset = s_assetAs<Rapture::ASkeletalMesh>(mesh);
    return asset != nullptr && asset->skeleton() == skeleton;
}

void SkeletonPreviewAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        AssetPickerConfig config{
            .types = {Rapture::ASSET_SKELETAL_MESH},
            .predicate = [this](Rapture::AssetHandle mesh, const Rapture::AssetMetadata &metadata) {
                (void)metadata;
                return s_meshUsesSkeleton(mesh, m_handle);
            },
        };
        rowAssetPicker(t, "Mesh", m_meshPicker, std::move(config),
                       [this](Rapture::AssetHandle mesh) { applyPreviewMesh(mesh); });
    });
}

void SkeletonPreviewAssetEditor::applyPreviewMesh(Rapture::AssetHandle mesh)
{
    Rapture::ASkeleton *asset = s_assetAs<Rapture::ASkeleton>(m_handle);
    if (asset == nullptr) {
        return;
    }

    std::vector<Rapture::AssetHandle> shown(asset->previewMeshes().begin(), asset->previewMeshes().end());
    for (Rapture::AssetHandle previous : shown) {
        asset->removePreviewMesh(previous);
    }

    if (!asset->addPreviewMesh(mesh)) {
        return;
    }

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_handle);
    if (!Rapture::AssetManager::saveAsset(m_handle, metadata.assetPath.parent_path())) {
        RP_ERROR("could not write '{}' back, its preview mesh is only set for this run", metadata.getName());
    }
}

void SkeletonPreviewAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::ASkeleton *asset = s_assetAs<Rapture::ASkeleton>(handle);
    if (asset == nullptr || !m_meshPicker) {
        return;
    }

    std::span<const Rapture::AssetHandle> shown = asset->previewMeshes();
    m_meshPicker->setAsset(shown.empty() ? Rapture::INVALID_ASSET_HANDLE : shown.front());
}

void SkeletonAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_jointCount = rowText(t, "Joints", "0");
        m_rootCount = rowText(t, "Roots", "0");
    });
}

void SkeletonAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::ASkeleton *asset = s_assetAs<Rapture::ASkeleton>(handle);
    if (asset == nullptr) {
        return;
    }

    const Rapture::Skeleton &skeleton = asset->skeleton();

    uint32_t roots = 0;
    for (Rapture::Skeleton::JointIndex parent : skeleton.getParents()) {
        if (parent == Rapture::Skeleton::INVALID_JOINT_INDEX) {
            ++roots;
        }
    }

    if (m_jointCount != nullptr) {
        m_jointCount->setText(std::to_string(skeleton.getJointCount()));
    }
    if (m_rootCount != nullptr) {
        m_rootCount->setText(std::to_string(roots));
    }
}
