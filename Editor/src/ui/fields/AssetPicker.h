#ifndef RAPTURE__ASSET_PICKER_H
#define RAPTURE__ASSET_PICKER_H

#include "ui/AssetThumbnail.h"

#include <rapture/assets/asset_manager/AssetCommon.h>

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class IconButton;

namespace Rapture {
struct AssetMetadata;
} // namespace Rapture

/**
 * @brief The set of assets a picker offers: a type filter, a name filter and an arbitrary extra test.
 * An empty filter means "no restriction on this axis".
 */
struct AssetQuery {
    std::vector<Rapture::AssetType> types;
    std::string nameFilter;
    std::function<bool(Rapture::AssetHandle, const Rapture::AssetMetadata &)> predicate = {};
    bool includeDisk = true;
    bool includeVirtual = true;
};

/**
 * @brief Collects every registered asset matching a query, closest name match first then alphabetically
 * @param query The query to run over the asset registry
 * @return The matching handles, in display order
 */
std::vector<Rapture::AssetHandle> AssetQuery_collect(const AssetQuery &query);

struct AssetPickerConfig {
    std::vector<Rapture::AssetType> types; // empty = every type
    std::function<bool(Rapture::AssetHandle, const Rapture::AssetMetadata &)> predicate = {};
    float rowHeight = 52.0f;
    int32_t maxVisibleRows = 10;
    float popupWidth = 0.0f; // 0 = match the picker's own width
    float previewSize = 56.0f;
    bool allowsEmpty = false; // shows the remove button, which unselects the asset
};

/**
 * @brief Editor asset field: the selected asset's thumbnail, name and type, which drops down the matching assets on
 * click and, where the field may be empty, unselects the asset from its remove button.
 * The dropdown rows are AssetMenuItemView rows in a ContextMenu, which takes the given theme classes.
 */
class AssetPicker {
  public:
    AssetPicker(Amethyst::UIScope &parent, AssetPickerConfig config, std::vector<std::string> classes = {});

    /**
     * @brief Selects an asset without firing onAssetSelected
     * @param handle The asset to show
     */
    void setAsset(Rapture::AssetHandle handle);
    Rapture::AssetHandle getAsset() const { return m_selected; }

    void open();
    void close();
    bool isOpen() const;

    Amethyst::Frame *getRoot() const { return m_root; }

  public:
    /**
     * @brief Called with the picked asset, or with no asset when the remove button unselects it
     */
    std::function<void(Rapture::AssetHandle)> onAssetSelected;

  private:
    void buildFace(Amethyst::UIScope &parent);

    /**
     * @brief Builds the name over the type beside the thumbnail
     * @param face Scope of the field's frame
     */
    void buildNameAndType(Amethyst::FrameScope &face);

    void buildRemoveButton();
    void buildMenu();
    void rebuildItems();
    void applySelection();
    void applyOpenState(bool open);
    void selectAsset(Rapture::AssetHandle handle);

    /**
     * @brief The type the picker offers
     * @return The type, or none when it offers several
     */
    Rapture::AssetType expectedType() const;

  private:
    AssetPickerConfig m_config;
    std::vector<std::string> m_classes;
    Rapture::AssetHandle m_selected = Rapture::INVALID_ASSET_HANDLE;

    Amethyst::Frame *m_root = nullptr;
    std::optional<AssetThumbnail> m_thumbnail;
    Amethyst::TextLabel *m_nameLabel = nullptr;
    Amethyst::TextLabel *m_typeLabel = nullptr;
    IconButton *m_removeButton = nullptr;
    Amethyst::ContextMenu *m_menu = nullptr;
};

#endif // RAPTURE__ASSET_PICKER_H
