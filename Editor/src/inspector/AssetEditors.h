#ifndef RAPTURE__ASSET_EDITORS_H
#define RAPTURE__ASSET_EDITORS_H

#include "Icons.h"
#include "inspector/AssetEditorBase.h"

#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {
class Bone3D;
} // namespace Rapture

/**
 * @brief The geometry an open mesh asset holds
 */
class MeshAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Geometry"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_triangleCount = nullptr;
    Amethyst::TextLabel *m_vertexCount = nullptr;
    Amethyst::TextLabel *m_bounds = nullptr;
    bool m_rayTraced = true;
};

/**
 * @brief The material each section of an open mesh asset is drawn with where nothing else is chosen
 */
class MeshMaterialAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Material Slots"; }
    const char *icon() const override { return Icons::SVG_MATERIAL; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    /**
     * @brief Points one of the open mesh's slots at a material and writes it back to disk
     * @param slot The slot to set
     * @param material The material the slot is to default to
     */
    void applyMaterialSlot(uint32_t slot, Rapture::AssetHandle material);

  private:
    std::vector<std::optional<AssetPicker>> m_materialPickers;
};

/**
 * @brief Which axis of a mesh's geometry points up
 */
enum MeshUpAxis {
    MESH_UP_AXIS_Y,
    MESH_UP_AXIS_Z,
    MESH_UP_AXIS_COUNT
};

constexpr std::string_view MESH_UP_AXIS_LABELS[MESH_UP_AXIS_COUNT] = {"Y up", "Z up"};

/**
 * @brief A scale and up axis to bake into an open mesh asset's geometry, shown on the mesh before it is saved
 */
struct MeshBakeTransform {
    float scale = 1.0f;
    MeshUpAxis upAxis = MESH_UP_AXIS_Y;
    std::function<void()> onChanged;
};

/**
 * @brief The scale and up axis an open mesh asset is to be baked with
 */
class MeshTransformAssetEditor : public AssetEditorBase {
  public:
    /**
     * @param bakeTransform The transform the rows edit, which outlives this editor
     */
    explicit MeshTransformAssetEditor(MeshBakeTransform &bakeTransform);

    const char *title() const override { return "Transform"; }
    const char *icon() const override { return Icons::SVG_TRANSFORM; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override { (void)handle; }

  private:
    MeshBakeTransform &m_bakeTransform;
};

/**
 * @brief Where an open mesh asset was imported from and what it was imported with
 */
class MeshImportAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Import"; }
    const char *icon() const override { return Icons::SVG_FOLDER_PLUS; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_sourcePath = nullptr;
    Amethyst::TextLabel *m_meshInFile = nullptr;
};

/**
 * @brief The skeleton an open skeletal mesh asset is bound to
 */
class MeshSkeletonAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Skeleton"; }
    const char *icon() const override { return Icons::SVG_SKELETON; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_skeletonName = nullptr;
    Amethyst::TextLabel *m_jointCount = nullptr;
};

/**
 * @brief The joint hierarchy an open skeleton asset holds
 */
class SkeletonAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Skeleton"; }
    const char *icon() const override { return Icons::SVG_SKELETON; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_jointCount = nullptr;
    Amethyst::TextLabel *m_rootCount = nullptr;
};

/**
 * @brief The meshes an open skeleton asset is shown on
 */
class SkeletonPreviewAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Preview Meshes"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    /**
     * @brief Shows the open skeleton on a different mesh in place of one it is shown on
     * @param index Which of the preview meshes to replace
     * @param mesh The skeletal mesh to show it on instead
     */
    void replacePreviewMesh(uint32_t index, Rapture::AssetHandle mesh);

    /**
     * @brief Stops showing the open skeleton on one of its preview meshes
     * @param index Which of the preview meshes to drop
     */
    void removePreviewMesh(uint32_t index);

    /**
     * @brief Shows the open skeleton on one more mesh
     * @param mesh The skeletal mesh to add
     */
    void addPreviewMesh(Rapture::AssetHandle mesh);

    /**
     * @brief Writes the open skeleton's preview meshes back to its asset file
     */
    void savePreviewMeshes();

  private:
    std::vector<std::optional<AssetPicker>> m_meshPickers;
    std::optional<AssetPicker> m_addMeshPicker;
};

/**
 * @brief Where one joint of an open skeleton asset rests and what it hangs from
 */
class SkeletonJointAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return m_jointName.c_str(); }
    const char *icon() const override { return Icons::SVG_BONE; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

    /**
     * @brief Points this editor at the joint a bone stands for, before its body is built
     * @param bone The bone whose joint is edited
     */
    void setBone(Rapture::Bone3D *bone);

  private:
    /**
     * @brief Writes the fields into the joint's rest transform and shows it on the pose
     */
    void applyRestTransform();

  private:
    Rapture::Bone3D *m_bone = nullptr;
    std::string m_jointName;
    Amethyst::TextLabel *m_parentName = nullptr;
    double m_values[3][3] = {};
    glm::quat m_shownRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief The image an open texture asset holds
 */
class TextureAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Texture"; }
    const char *icon() const override { return Icons::SVG_LAYERS; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_resolution = nullptr;
    Amethyst::TextLabel *m_format = nullptr;
    Amethyst::TextLabel *m_mipCount = nullptr;
    Amethyst::TextLabel *m_gpuMemory = nullptr;
};

/**
 * @brief How an open texture asset is sampled
 */
class TextureSamplingAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Sampling"; }
    const char *icon() const override { return Icons::SVG_GRID; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override { (void)ch; }
    void sync(Rapture::AssetHandle handle) override { (void)handle; }
};

/**
 * @brief How an open texture asset is imported from its source
 */
class TextureImportAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Import"; }
    const char *icon() const override { return Icons::SVG_FOLDER_PLUS; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override { (void)ch; }
    void sync(Rapture::AssetHandle handle) override { (void)handle; }
};

#endif // RAPTURE__ASSET_EDITORS_H
