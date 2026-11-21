#ifndef RAPTURE__CONTEXT_MENU_ITEMS_H
#define RAPTURE__CONTEXT_MENU_ITEMS_H

#include "components/checkbox.h"
#include "components/context_menu.h"
#include "components/radio_button.h"
#include "ui/AssetThumbnail.h"
#include <rapture/assets/asset_manager/AssetCommon.h>

#include <components/common.h>
#include <components/drag.h>
#include <components/frame.h>
#include <components/image_label.h>
#include <components/text_label.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AM = Amethyst;

namespace Rapture {
struct TypeInfo;
} // namespace Rapture

class AssetMenuItem : public AM::ContextMenu::ActionItemData {
  public:
    std::function<void(AM::UIObject &row)> content; // empty = default text-row builder

    static std::unique_ptr<AM::ContextMenu::ItemData> create(Rapture::AssetHandle asset);

  public:
    Rapture::AssetHandle assetHandle = Rapture::INVALID_ASSET_HANDLE;
    Rapture::AssetType assetType = Rapture::ASSET_NONE;
    const Rapture::TypeInfo *authoredClass = nullptr;
    Amethyst::AmTextureId thumbnail = AM::AM_INVALID_TEXTURE;
    std::string name;
    std::string typeLabel;
};

class AssetMenuItemView : public AM::ContextMenu::ActionItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    std::optional<AssetThumbnail> m_thumbnail;
    AM::TextLabel *m_nameLabel = nullptr;
    AM::TextLabel *m_typeLabel = nullptr;
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

class AddAssetMenuItem : public AM::ContextMenu::ActionItemData {
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

class AddAssetMenuItemView : public AM::ContextMenu::ActionItemView {
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

class ToggleMenuItem : public AM::ContextMenu::ToggleItemData {
  public:
    explicit ToggleMenuItem(std::function<void(bool)> cb = {}) : ToggleItemData(std::move(cb)) {}

    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label, std::function<void(bool)> cb = {},
                                                             std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

class ToggleMenuItemView : public AM::ContextMenu::ToggleItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::Checkbox *m_checkbox = nullptr;
    AM::ImageLabel *m_icon = nullptr;
    AM::TextLabel *m_label = nullptr;
};

class RadioMenuItem : public AM::ContextMenu::RadioItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label, AM::RadioGroup *group, int32_t value,
                                                             std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

class RadioMenuItemView : public AM::ContextMenu::RadioItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::RadioButton *m_radio = nullptr;
    AM::ImageLabel *m_icon = nullptr;
    AM::TextLabel *m_label = nullptr;
};

class NumberMenuItem : public AM::ContextMenu::NumberItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label, double value, std::function<void(double)> onChanged);

  public:
    std::string label;
    bool enabled = true;
};

class NumberMenuItemView : public AM::ContextMenu::NumberItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::TextLabel *m_label = nullptr;
    AM::DragFloat *m_drag = nullptr;
};

class SeparatorMenuItem : public AM::ContextMenu::SeparatorItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData> create(std::string label = {});

  public:
    std::string label; // empty = plain line, no text
};

class SeparatorMenuItemView : public AM::ContextMenu::SeparatorItemView {
  public:
    AM::Frame *create(AM::ContextMenu &owner) override;
    void bind(AM::ContextMenu::ItemData &item) override;

  private:
    AM::TextLabel *m_label = nullptr;
    AM::Frame *m_line = nullptr;
};

class SubmenuItem : public AM::ContextMenu::SubmenuItemData {
  public:
    static std::unique_ptr<AM::ContextMenu::ItemData>
    create(std::string label, std::vector<std::unique_ptr<AM::ContextMenu::ItemData>> items, std::string svgIcon = {});

  public:
    std::string label;
    std::string svgIcon; // empty = no icon
    bool enabled = true;
};

class SubmenuItemView : public AM::ContextMenu::SubmenuItemView {
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

#endif // RAPTURE__CONTEXT_MENU_ITEMS_H
