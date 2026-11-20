#include "AssetEditors.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/meshes/AMesh.h>
#include <rapture/assets/meshes/ASkeletalMesh.h>
#include <rapture/assets/meshes/AStaticMesh.h>
#include <rapture/assets/skeletons/ASkeleton.h>
#include <rapture/assets/textures/ATexture.h>
#include <rapture/core/utils/Log.h>

#include <cstdio>
#include <iterator>
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

static constexpr float MESH_BAKE_SCALE_MIN = 0.001f;
static constexpr float MESH_BAKE_SCALE_MAX = 10.0f;

static std::string s_formatCount(uint64_t count)
{
    std::string digits = std::to_string(count);
    std::string grouped;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            grouped += ',';
        }
        grouped += digits[i];
    }
    return grouped;
}

static std::string s_formatExtent(const glm::vec3 &extent)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f x %.2f x %.2f m", extent.x, extent.y, extent.z);
    return buffer;
}

void MeshAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_triangleCount = rowText(t, "Triangles", "");
        m_vertexCount = rowText(t, "Vertices", "");
        m_bounds = rowText(t, "Bounds", "");
        // TODO: whether the mesh takes part in ray tracing
        rowCheckbox(t, "Ray tracing", &m_rayTraced, [](bool rayTraced) { (void)rayTraced; });
    });
}

void MeshAssetEditor::sync(Rapture::AssetHandle handle)
{
    if (!subjectChanged()) {
        return;
    }

    const Rapture::Mesh *mesh = s_meshGeometry(handle);
    if (mesh == nullptr) {
        return;
    }

    m_triangleCount->setText(s_formatCount(mesh->getIndexCount() / 3));
    m_vertexCount->setText(s_formatCount(mesh->getVertexCount()));
    m_bounds->setText(s_formatExtent(mesh->getBoundsMax() - mesh->getBoundsMin()));
}

void MeshMaterialAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    // the mesh decides how many slots there are, so the rows are built against the open asset
    const Rapture::AMesh *mesh = s_meshAsset(m_handle);
    m_materialPickers.clear();
    m_materialPickers.resize(mesh != nullptr ? mesh->materialSlots().size() : 0);

    fieldTable(ch, [this](Amethyst::TableScope &t) {
        for (uint32_t slot = 0; slot < m_materialPickers.size(); slot++) {
            rowAssetPicker(t, "Slot " + std::to_string(slot), m_materialPickers[slot], {.types = {Rapture::ASSET_MATERIAL_INSTANCE}},
                           [this, slot](Rapture::AssetHandle material) { applyMaterialSlot(slot, material); });
        }
    });
}

void MeshMaterialAssetEditor::applyMaterialSlot(uint32_t slot, Rapture::AssetHandle material)
{
    Rapture::AMesh *mesh = s_meshAsset(m_handle);
    if (mesh == nullptr) {
        return;
    }

    mesh->setMaterialSlot(slot, material);

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_handle);
    if (!Rapture::AssetManager::saveAsset(m_handle, metadata.assetPath.parent_path())) {
        RP_ERROR("could not write '{}' back, its default materials are only set for this run", metadata.getName());
    }
}

void MeshMaterialAssetEditor::sync(Rapture::AssetHandle handle)
{
    const Rapture::AMesh *mesh = s_meshAsset(handle);
    if (mesh == nullptr) {
        return;
    }

    if (mesh->materialSlots().size() != m_materialPickers.size()) {
        if (requestRefresh) {
            requestRefresh();
        }
        return;
    }

    for (uint32_t slot = 0; slot < m_materialPickers.size(); slot++) {
        if (m_materialPickers[slot].has_value()) {
            m_materialPickers[slot]->setAsset(mesh->materialSlot(slot));
        }
    }
}

MeshTransformAssetEditor::MeshTransformAssetEditor(MeshBakeTransform &bakeTransform) : m_bakeTransform(bakeTransform) {}

void MeshTransformAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    std::vector<std::string> upAxisOptions(std::begin(MESH_UP_AXIS_LABELS), std::end(MESH_UP_AXIS_LABELS));

    fieldTable(ch, [this, &upAxisOptions](Amethyst::TableScope &t) {
        rowSlider(
            t, "Scale", &m_bakeTransform.scale, MESH_BAKE_SCALE_MIN, MESH_BAKE_SCALE_MAX,
            [this](float scale) {
                (void)scale;
                if (m_bakeTransform.onChanged != nullptr) {
                    m_bakeTransform.onChanged();
                }
            },
            "%.3f");
        rowDropdown(t, "Up axis", MESH_UP_AXIS_LABELS[m_bakeTransform.upAxis], upAxisOptions, [this](int axis) {
            m_bakeTransform.upAxis = static_cast<MeshUpAxis>(axis);
            if (m_bakeTransform.onChanged != nullptr) {
                m_bakeTransform.onChanged();
            }
        });
    });
}

void MeshImportAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_sourcePath = rowText(t, "Source", "");
        m_meshInFile = rowText(t, "Mesh in file", "");
        // TODO: read both from the mesh's import config once mesh imports record one
        rowText(t, "Scale", "1.000");
        rowText(t, "Up axis", MESH_UP_AXIS_LABELS[MESH_UP_AXIS_Y]);
    });
}

void MeshImportAssetEditor::sync(Rapture::AssetHandle handle)
{
    if (!subjectChanged()) {
        return;
    }

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle);
    if (!metadata.provenance.has_value()) {
        m_sourcePath->setText("Generated");
        m_meshInFile->setText("");
        return;
    }

    const Rapture::AssetProvenance &provenance = *metadata.provenance;
    m_sourcePath->setText(provenance.sourcePath.generic_string());
    m_meshInFile->setText(provenance.sourceSubIndex.has_value() ? std::to_string(*provenance.sourceSubIndex) : "");
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
            .predicate =
                [this](Rapture::AssetHandle mesh, const Rapture::AssetMetadata &metadata) {
                    (void)metadata;
                    return s_meshUsesSkeleton(mesh, m_handle);
                },
        };
        rowAssetPicker(t, "Mesh", m_meshPicker, std::move(config), [this](Rapture::AssetHandle mesh) { applyPreviewMesh(mesh); });
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

static std::string_view s_textureFormatName(Rapture::TextureFormat format)
{
    switch (format) {
    case Rapture::TextureFormat::RGBA8:
        return "RGBA8";
    case Rapture::TextureFormat::RGB8:
        return "RGB8";
    case Rapture::TextureFormat::BGRA8:
        return "BGRA8";
    case Rapture::TextureFormat::RGBA16F:
        return "RGBA16F";
    case Rapture::TextureFormat::RGB16F:
        return "RGB16F";
    case Rapture::TextureFormat::RGBA32F:
        return "RGBA32F";
    case Rapture::TextureFormat::RGB32F:
        return "RGB32F";
    case Rapture::TextureFormat::R11G11B10F:
        return "R11G11B10F";
    case Rapture::TextureFormat::RG16F:
        return "RG16F";
    case Rapture::TextureFormat::R16F:
        return "R16F";
    case Rapture::TextureFormat::R32F:
        return "R32F";
    case Rapture::TextureFormat::R32UI:
        return "R32UI";
    case Rapture::TextureFormat::R8UI:
        return "R8UI";
    case Rapture::TextureFormat::R8U:
        return "R8U";
    case Rapture::TextureFormat::BC1_RGB:
        return "BC1 RGB";
    case Rapture::TextureFormat::BC1_RGBA:
        return "BC1 RGBA";
    case Rapture::TextureFormat::BC3:
        return "BC3";
    case Rapture::TextureFormat::BC4:
        return "BC4";
    case Rapture::TextureFormat::BC5:
        return "BC5";
    case Rapture::TextureFormat::BC7:
        return "BC7";
    case Rapture::TextureFormat::BC6H:
        return "BC6H";
    case Rapture::TextureFormat::D32F:
        return "D32F";
    case Rapture::TextureFormat::D24S8:
        return "D24S8";
    }
    return "Unknown";
}

static std::string s_formatTextureFormat(const Rapture::TextureSpecification &spec)
{
    std::string name(s_textureFormatName(spec.format));
    const bool hasSrgbForm = Rapture::toVkFormat(spec.format, true) != Rapture::toVkFormat(spec.format, false);
    if (spec.srgb && hasSrgbForm) {
        name += ", sRGB";
    }
    return name;
}

static std::string s_formatMegabytes(uint64_t bytes)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

void TextureAssetEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_resolution = rowText(t, "Resolution", "");
        m_format = rowText(t, "Format", "");
        m_mipCount = rowText(t, "Mips", "");
        m_gpuMemory = rowText(t, "GPU memory", "");
    });
}

void TextureAssetEditor::sync(Rapture::AssetHandle handle)
{
    if (!subjectChanged()) {
        return;
    }

    auto *texture = s_assetAs<Rapture::ATexture>(handle);
    if (texture == nullptr) {
        return;
    }

    const Rapture::TextureSpecification &spec = texture->texture().getSpecification();
    if (m_resolution != nullptr) {
        m_resolution->setText(std::to_string(spec.width) + " x " + std::to_string(spec.height));
    }
    if (m_format != nullptr) {
        m_format->setText(s_formatTextureFormat(spec));
    }
    if (m_mipCount != nullptr) {
        m_mipCount->setText(std::to_string(spec.mipLevels));
    }
    if (m_gpuMemory != nullptr) {
        m_gpuMemory->setText(s_formatMegabytes(texture->texture().getSizeBytes()));
    }
}
