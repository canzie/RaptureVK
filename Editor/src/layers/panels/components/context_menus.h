#ifndef RAPTURE__CONTEXT_MENUS_H
#define RAPTURE__CONTEXT_MENUS_H

#include "asset_manager/AssetCommon.h"
#include "components/checkbox.h"
#include "components/context_menu.h"
#include "components/radio_button.h"

#include <components/common.h>
#include <components/frame.h>
#include <components/image_label.h>
#include <components/text_label.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AM = Amethyst;

namespace Rapture {
struct TypeInfo;
} // namespace Rapture

// Asset Context Menu Action Item Data
class AssetContextMenuAID : public AM::ContextMenu::ActionItemData {
  public:
    std::function<void(AM::UIObject &row)> content; // empty = default text-row builder

    static std::unique_ptr<AM::ContextMenu::ItemData> create(Rapture::AssetHandle asset);

  public:
    Rapture::AssetHandle assetHandle = Rapture::INVALID_ASSET_HANDLE;
    Rapture::AssetType assetType = Rapture::ASSET_NONE;
    const Rapture::TypeInfo *authoredClass = nullptr;
    Amethyst::AmTextureId thumbnail = AM::AM_INVALID_TEXTURE;
    std::string name;
};

// Asset Context Menu Action Item View
class AssetContextMenuAIV : public AM::ContextMenu::ActionItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::ImageLabel *m_preview = nullptr;
    AM::Frame *m_assetTypeAccent = nullptr;
    AM::TextLabel *m_label = nullptr;
};

/**
 * @brief How an add menu row draws itself
 */
enum AddAssetRowStyle {
    ADD_ROW_ICON,  // leading icon beside one line of text
    ADD_ROW_ASSET, // icon in a well carrying the asset type's accent bar
    ADD_ROW_PLAIN, // text alone
    ADD_ROW_COUNT
};

// Add Asset Context Menu Action Item Data
class AddAssetContextMenuAID : public AM::ContextMenu::ActionItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> createIconRow(std::string label, std::string svgIcon,
                                                                    std::function<void()> onActivate);

    /**
     * @brief A row creating an asset, taking its icon and accent from the type it creates
     * @param label The row's text
     * @param assetType The type the row creates
     * @param onActivate What to run when the row is picked
     * @return The item, ready to hand to a ContextMenu
     */
    static std::unique_ptr<AM::ContextMenu::ItemData> createAssetRow(std::string label, Rapture::AssetType assetType,
                                                                     std::function<void()> onActivate);

    static std::unique_ptr<AM::ContextMenu::ItemData> createPlainRow(std::string label, std::function<void()> onActivate);

  public:
    AddAssetRowStyle style = ADD_ROW_PLAIN;
    std::string label;
    std::string svgIcon;
    Rapture::AssetType assetType = Rapture::ASSET_NONE;
    const Rapture::TypeInfo *authoredClass = nullptr;
    bool enabled = true;
};

// Add Asset Context Menu Action Item View
class AddAssetContextMenuAIV : public AM::ContextMenu::ActionItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;
    float rowHeight(const AM::ContextMenu &owner) const override;

  private:
    AM::Frame *m_iconWell = nullptr;
    AM::ImageLabel *m_icon = nullptr;
    AM::Frame *m_accent = nullptr;
    AM::TextLabel *m_label = nullptr;
};

// Viewport Context Menu Toggle Item Data
class ViewportContextMenuTID : public AM::ContextMenu::ToggleItemData {
  public:
    explicit ViewportContextMenuTID(std::function<void(bool)> cb = {}) : ToggleItemData(std::move(cb)) {}

    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label, std::function<void(bool)> cb = {},
                                                             std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

// Viewport Context Menu Toggle Item View
class ViewportContextMenuTIV : public AM::ContextMenu::ToggleItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::Checkbox *m_checkbox = nullptr;
    AM::ImageLabel *m_icon = nullptr;
    AM::TextLabel *m_label = nullptr;
};

// Viewport Context Menu Radio Item Data
class ViewportContextMenuRID : public AM::ContextMenu::RadioItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label, AM::RadioGroup *group, int32_t value,
                                                             std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

// Viewport Context Menu Radio Item View
class ViewportContextMenuRIV : public AM::ContextMenu::RadioItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::RadioButton *m_radio = nullptr;
    AM::ImageLabel *m_icon = nullptr;
    AM::TextLabel *m_label = nullptr;
};

// Viewport Context Menu Separator Item Data
class ViewportContextMenuSID : public AM::ContextMenu::SeparatorItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label = {});

  public:
    std::string label; // empty = plain line, no text
};

// Viewport Context Menu Separator Item View
class ViewportContextMenuSIV : public AM::ContextMenu::SeparatorItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::TextLabel *m_label = nullptr;
    AM::Frame *m_line = nullptr;
};

// Viewport Context Menu Submenu Item Data
class ViewportContextMenuMID : public AM::ContextMenu::SubmenuItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData>
    create(std::string label, std::vector<std::unique_ptr<AM::ContextMenu::ItemData>> items, std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

// Viewport Context Menu Submenu Item View
class ViewportContextMenuMIV : public AM::ContextMenu::SubmenuItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::ImageLabel *m_icon = nullptr;
    AM::TextLabel *m_label = nullptr;
    AM::Frame *m_badge = nullptr; // item-count pill, shown when the submenu isn't empty
    AM::TextLabel *m_badgeText = nullptr;
    AM::ImageLabel *m_arrow = nullptr;
};

#endif // RAPTURE__CONTEXT_MENUS_H
