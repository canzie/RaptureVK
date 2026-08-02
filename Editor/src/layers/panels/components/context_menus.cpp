#include "context_menus.h"

#include "amethyst/icons.h"
#include "asset_manager/AssetCommon.h"
#include "asset_manager/AssetManager.h"
#include "asset_visuals.h"
#include "components/context_menu.h"

#include <components/common.h>
#include <components/frame.h>
#include <components/image_label.h>
#include <components/text_label.h>

using namespace Amethyst;

static constexpr float ROW_PADDING = 8.0f;
static constexpr float THUMB_INSET = 4.0f;
static constexpr float THUMB_GAP = 6.0f;
static constexpr float ACCENT_HEIGHT = 2.0f;
static constexpr float ICON_INSET = 4.0f;
static constexpr float ICON_GAP = 10.0f;
static constexpr float RADIO_DIAMETER_SCALE = 0.4f;
static constexpr float SEPARATOR_GAP = 8.0f;

// TODO: replace with a real measurement once Amethyst exposes text width (e.g. TextLabel::measureText/getTextSize)
static float s_estimateTextWidth(const std::string &text, float fontSize)
{
    return static_cast<float>(text.size()) * fontSize * 0.55f + 4.0f;
}

std::unique_ptr<ContextMenu::ItemData> AssetContextMenuAID::create(Rapture::AssetHandle asset)
{
    auto item = std::make_unique<AssetContextMenuAID>();
    item->assetHandle = asset;

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(asset);
    item->assetType = metadata.assetType;
    item->name = metadata.name;

    return item;
}

Frame *AssetContextMenuAIV::create(ContextMenu &owner)
{
    Frame *row = ActionItemView::create(owner);

    m_preview = row->add<ImageLabel>();
    m_preview->setBaseStyleProperties({
        .backgroundColor = Color3{0.1f, 0.1f, 0.1f},
        .backgroundTransparency = 0.0f,
        .borderPixelSize = 0.0f,
    });
    m_preview->setBaseProperties({
        .anchorPoint = {0.0f, 0.5f},
        .interactable = false,
        .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
    });

    m_assetTypeAccent = m_preview->add<Frame>();
    m_assetTypeAccent->setBaseStyleProperties({.borderPixelSize = 0.0f});
    m_assetTypeAccent->setBaseProperties({
        .anchorPoint = {0.0f, 1.0f},
        .interactable = false,
        .position = UDim2::fromScale(0.0f, 1.0f),
        .size = UDim2(1.0f, 0.0f, 0.0f, ACCENT_HEIGHT),
    });

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({
        .interactable = false,
        .size = UDim2::fromScale(1.0f, 1.0f),
    });

    return row;
}

void AssetContextMenuAIV::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &asset = item.as<AssetContextMenuAID>();

    if (asset.content) {
        m_preview->setBaseProperties({.visible = false});
        m_label->setBaseProperties({.visible = false});
        asset.content(*m_row);
        return;
    }

    float thumbSize = m_owner->itemHeight - THUMB_INSET;
    m_preview->setBaseProperties({.size = UDim2::fromOffset(thumbSize, thumbSize), .visible = true});
    if (asset.thumbnail == AM_INVALID_TEXTURE) {
        m_preview->setSvg(Asset_iconForType(asset.assetType));
    } else {
        m_preview->setImage(asset.thumbnail);
    }

    m_assetTypeAccent->setBaseStyleProperties({.backgroundColor = Asset_colorForType(asset.assetType)});

    m_label->setBaseProperties({
        .padding = UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(ROW_PADDING), UDim::fromOffset(0.0f),
                         UDim::fromOffset(ROW_PADDING + thumbSize + THUMB_GAP)},
        .visible = true,
    });
    m_label->setText(asset.name);
}

std::unique_ptr<ContextMenu::ItemData> ViewportContextMenuTID::create(std::string label, std::function<void(bool)> cb,
                                                                      std::string svgIcon)
{
    auto item = std::make_unique<ViewportContextMenuTID>(std::move(cb));
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);

    return item;
}

Frame *ViewportContextMenuTIV::create(ContextMenu &owner)
{
    Frame *row = ToggleItemView::create(owner);

    m_checkbox = row->add<Checkbox>();
    m_checkbox->setBaseProperties({
        .anchorPoint = {0.0f, 0.5f},
        .interactable = false, // decorative only since the row handles the click
        .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
    });

    m_icon = row->add<ImageLabel>();
    m_icon->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_icon->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .interactable = false});

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({.interactable = false, .size = UDim2::fromScale(1.0f, 1.0f)});

    return row;
}

void ViewportContextMenuTIV::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &toggleItem = item.as<ViewportContextMenuTID>();

    float iconSize = m_owner->itemHeight - ICON_INSET;
    m_checkbox->setBaseProperties({.size = UDim2::fromOffset(iconSize, iconSize)});
    m_checkbox->value = &toggleItem.value;
    m_checkbox->markDirty();

    float labelLeft = ROW_PADDING + iconSize + ICON_GAP;
    bool hasIcon = !toggleItem.svgIcon.empty();
    m_icon->setBaseProperties({.visible = hasIcon});
    if (hasIcon) {
        m_icon->setSvg(toggleItem.svgIcon);
        m_icon->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
        m_icon->setBaseProperties({.position = UDim2(0.0f, labelLeft, 0.5f, 0.0f), .size = UDim2::fromOffset(iconSize, iconSize)});
        labelLeft += iconSize + ICON_GAP;
    }

    m_label->setBaseProperties({
        .padding =
            UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(ROW_PADDING), UDim::fromOffset(0.0f), UDim::fromOffset(labelLeft)},
    });
    m_label->setText(toggleItem.label);
    m_row->setBaseProperties({.interactable = toggleItem.enabled});
}

std::unique_ptr<ContextMenu::ItemData> ViewportContextMenuRID::create(std::string label, RadioGroup *group, int32_t value,
                                                                      std::string svgIcon)
{
    auto item = std::make_unique<ViewportContextMenuRID>();
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);
    item->group = group;
    item->value = value;

    return item;
}

Frame *ViewportContextMenuRIV::create(ContextMenu &owner)
{
    Frame *row = RadioItemView::create(owner);

    m_radio = row->add<RadioButton>();
    m_radio->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f)});

    m_icon = row->add<ImageLabel>();
    m_icon->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_icon->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .interactable = false});

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({.interactable = false, .size = UDim2::fromScale(1.0f, 1.0f)});

    return row;
}

void ViewportContextMenuRIV::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &radioItem = item.as<ViewportContextMenuRID>();

    float iconSize = m_owner->itemHeight - ICON_INSET;
    float radioSize = iconSize * RADIO_DIAMETER_SCALE;
    m_radio->setBaseProperties({.size = UDim2::fromOffset(radioSize, radioSize)});
    m_radio->value = radioItem.value;
    m_radio->setGroup(radioItem.group);

    float labelLeft = ROW_PADDING + radioSize + ICON_GAP;
    bool hasIcon = !radioItem.svgIcon.empty();
    m_icon->setBaseProperties({.visible = hasIcon});
    if (hasIcon) {
        m_icon->setSvg(radioItem.svgIcon);
        m_icon->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
        m_icon->setBaseProperties({.position = UDim2(0.0f, labelLeft, 0.5f, 0.0f), .size = UDim2::fromOffset(iconSize, iconSize)});
        labelLeft += iconSize + ICON_GAP;
    }

    m_label->setBaseProperties({
        .padding =
            UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(ROW_PADDING), UDim::fromOffset(0.0f), UDim::fromOffset(labelLeft)},
    });
    m_label->setText(radioItem.label);
    m_row->setBaseProperties({.interactable = radioItem.enabled});
}

std::unique_ptr<ContextMenu::ItemData> ViewportContextMenuSID::create(std::string label)
{
    auto item = std::make_unique<ViewportContextMenuSID>();
    item->label = std::move(label);

    return item;
}

Frame *ViewportContextMenuSIV::create(ContextMenu &owner)
{
    Frame *row = SeparatorItemView::create(owner);
    row->setBaseProperties({.interactable = false});

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties({
        .fontSize = owner.getTextStyleProperties().fontSize,
        .textXAlignment = owner.getTextStyleProperties().textXAlignment,
        .textYAlignment = owner.getTextStyleProperties().textYAlignment,
    });
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties(
        {.anchorPoint = {0.0f, 0.5f}, .interactable = false, .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f)});

    m_line = row->add<Frame>();
    m_line->setBaseStyleProperties({.borderPixelSize = 0.0f});
    m_line->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .interactable = false});

    return row;
}

void ViewportContextMenuSIV::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &sep = item.as<ViewportContextMenuSID>();

    if (sep.label.empty()) {
        m_label->setBaseProperties({.visible = false});
        m_line->setBaseProperties({
            .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
            .size = UDim2(1.0f, -(ROW_PADDING * 2.0f), 0.0f, 1.0f),
        });
        return;
    }

    float labelWidth = s_estimateTextWidth(sep.label, m_owner->getTextStyleProperties().fontSize);
    m_label->setBaseProperties({.size = UDim2(0.0f, labelWidth, 1.0f, 0.0f), .visible = true});
    m_label->setText(sep.label);

    float lineLeft = ROW_PADDING + labelWidth + SEPARATOR_GAP;
    m_line->setBaseProperties({
        .position = UDim2(0.0f, lineLeft, 0.5f, 0.0f),
        .size = UDim2(1.0f, -(lineLeft + ROW_PADDING), 0.0f, 1.0f),
    });
}

std::unique_ptr<ContextMenu::ItemData>
ViewportContextMenuMID::create(std::string label, std::vector<std::unique_ptr<ContextMenu::ItemData>> items, std::string svgIcon)
{
    auto item = std::make_unique<ViewportContextMenuMID>();
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);
    item->items = std::move(items);

    return item;
}

Frame *ViewportContextMenuMIV::create(ContextMenu &owner)
{
    Frame *row = SubmenuItemView::create(owner);

    m_icon = row->add<ImageLabel>();
    m_icon->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_icon->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .interactable = false});

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({.interactable = false, .size = UDim2::fromScale(1.0f, 1.0f)});

    m_badge = row->add<Frame>();
    m_badge->setBaseStyleProperties(
        {.backgroundColor = Color3{0.32f, 0.32f, 0.32f}, .backgroundTransparency = 0.0f, .borderPixelSize = 0.0f});
    m_badge->setBaseProperties({.anchorPoint = {1.0f, 0.5f}, .interactable = false});

    m_badgeText = m_badge->add<TextLabel>();
    m_badgeText->setTextStyleProperties({.textXAlignment = TextXAlignment::CENTER, .textYAlignment = TextYAlignment::CENTER});
    m_badgeText->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_badgeText->setBaseProperties({.interactable = false, .size = UDim2::fromScale(1.0f, 1.0f)});

    m_arrow = row->add<ImageLabel>();
    m_arrow->setSvg(Icons::ARROW);
    m_arrow->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_arrow->setBaseProperties(
        {.anchorPoint = {1.0f, 0.5f}, .interactable = false, .position = UDim2(1.0f, -ROW_PADDING, 0.5f, 0.0f)});

    return row;
}

void ViewportContextMenuMIV::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &submenu = item.as<ViewportContextMenuMID>();

    float iconSize = m_owner->itemHeight - ICON_INSET;
    float arrowSize = iconSize * 0.6f;

    bool hasIcon = !submenu.svgIcon.empty();
    m_icon->setBaseProperties({.visible = hasIcon});
    float labelLeft = ROW_PADDING;
    if (hasIcon) {
        m_icon->setSvg(submenu.svgIcon);
        m_icon->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
        m_icon->setBaseProperties({.position = UDim2(0.0f, labelLeft, 0.5f, 0.0f), .size = UDim2::fromOffset(iconSize, iconSize)});
        labelLeft += iconSize + ICON_GAP;
    }

    float labelRight = ROW_PADDING + arrowSize + ICON_GAP;
    bool hasBadge = !submenu.items.empty();
    m_badge->setBaseProperties({.visible = hasBadge});
    if (hasBadge) {
        std::string countText = std::to_string(submenu.items.size());
        float badgeWidth = s_estimateTextWidth(countText, m_owner->getTextStyleProperties().fontSize * 0.85f);
        m_badge->setBaseProperties({
            .position = UDim2(1.0f, -(labelRight + badgeWidth), 0.5f, 0.0f),
            .size = UDim2::fromOffset(badgeWidth, iconSize * 0.75f),
        });
        m_badgeText->setText(countText);
        labelRight += badgeWidth + ICON_GAP;
    }

    m_label->setBaseProperties({
        .padding = UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(labelRight), UDim::fromOffset(0.0f), UDim::fromOffset(labelLeft)},
    });
    m_label->setText(submenu.label);

    m_arrow->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
    m_arrow->setBaseProperties({.size = UDim2::fromOffset(arrowSize, arrowSize)});

    m_row->setBaseProperties({.interactable = submenu.enabled});
}
