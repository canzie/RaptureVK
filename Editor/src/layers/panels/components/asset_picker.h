#ifndef RAPTURE__ASSET_PICKER_H
#define RAPTURE__ASSET_PICKER_H

#include "asset_manager/AssetCommon.h"

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <functional>
#include <string>
#include <vector>

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
    std::function<bool(Rapture::AssetHandle, const Rapture::AssetMetadata &)> predicate;
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
    std::function<bool(Rapture::AssetHandle, const Rapture::AssetMetadata &)> predicate;
    float rowHeight = 47.0f;
    int32_t maxVisibleRows = 10;
    float popupWidth = 0.0f; // 0 = match the picker's own width
    float previewSize = 18.0f;
};

/**
 * @brief Editor asset field: a bar showing the selected asset that drops down the matching assets on click.
 * The dropdown rows are AssetContextMenuAIV rows in a ContextMenu. The given theme classes are applied to every
 * component it builds, the popup included.
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
    std::function<void(Rapture::AssetHandle)> onAssetSelected;

  private:
    void buildFace(Amethyst::UIScope &parent);
    void buildMenu();
    void rebuildItems();
    void applySelection();
    void applyOpenState(bool open);
    void selectAsset(Rapture::AssetHandle handle);

  private:
    AssetPickerConfig m_config;
    std::vector<std::string> m_classes;
    Rapture::AssetHandle m_selected = Rapture::INVALID_ASSET_HANDLE;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::ImageLabel *m_preview = nullptr;
    Amethyst::Frame *m_typeAccent = nullptr;
    Amethyst::TextLabel *m_label = nullptr;
    Amethyst::ImageLabel *m_arrow = nullptr;
    Amethyst::ContextMenu *m_menu = nullptr;
};

#endif // RAPTURE__ASSET_PICKER_H
