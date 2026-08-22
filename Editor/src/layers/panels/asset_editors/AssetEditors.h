#ifndef RAPTURE__ASSET_EDITORS_H
#define RAPTURE__ASSET_EDITORS_H

#include "Icons.h"
#include "layers/panels/asset_editors/AssetEditorBase.h"

#include <optional>

/**
 * @brief The geometry an open mesh asset holds
 */
class MeshAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Mesh"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    Amethyst::TextLabel *m_indexCount = nullptr;
    Amethyst::TextLabel *m_boundsMin = nullptr;
    Amethyst::TextLabel *m_boundsMax = nullptr;
};

/**
 * @brief The material an open mesh asset is drawn with where nothing else is chosen
 */
class MeshMaterialAssetEditor : public AssetEditorBase {
  public:
    const char *title() const override { return "Material"; }
    const char *icon() const override { return Icons::SVG_MATERIAL; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::AssetHandle handle) override;

  private:
    /**
     * @brief Points the open mesh at a material and writes it back to disk
     * @param material The material to default to
     */
    void applyDefaultMaterial(Rapture::AssetHandle material);

  private:
    std::optional<AssetPicker> m_materialPicker;
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
     * @brief Shows the open skeleton on a mesh in place of whatever it was shown on
     * @param mesh The skeletal mesh to show it on
     */
    void applyPreviewMesh(Rapture::AssetHandle mesh);

  private:
    std::optional<AssetPicker> m_meshPicker;
};

#endif // RAPTURE__ASSET_EDITORS_H
