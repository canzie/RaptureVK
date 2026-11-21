#include "ContextMenuItems.h"

#include "amethyst/icons.h"
#include "components/context_menu.h"
#include "ui/AssetVisuals.h"
#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/assets/asset_manager/AssetManager.h>

#include <components/common.h>
#include <components/frame.h>
#include <components/image_label.h>
#include <components/text_label.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_list_layout.h>

using namespace Amethyst;

static constexpr float ROW_PADDING = 8.0f;
static constexpr float THUMB_INSET = 4.0f;
static constexpr float ACCENT_HEIGHT = 2.0f;
static constexpr float THUMB_GAP = 10.0f;
static constexpr float MENU_NAME_HEIGHT = 17.0f;
static constexpr float MENU_TYPE_HEIGHT = 15.0f;
static constexpr std::string_view MENU_TYPE_CLASS = "asset-field-type";
static constexpr float ICON_INSET = 4.0f;
static constexpr float ICON_GAP = 10.0f;
static constexpr float MENU_CHECKBOX_SIZE = 16.0f;
static constexpr float MENU_RADIO_SIZE = 12.0f;
static constexpr float SEPARATOR_GAP = 8.0f;
static constexpr float NUMBER_FIELD_WIDTH_SCALE = 0.45f;
static constexpr std::string_view NUMBER_FIELD_CLASS = "property-input-field";
static constexpr float ADD_ASSET_ROW_HEIGHT = 44.0f;
static constexpr float ADD_ASSET_WELL_SIZE = 32.0f;
static constexpr float ADD_ASSET_ICON_SIZE = 26.0f;

// TODO: replace with a real measurement once Amethyst exposes text width (e.g. TextLabel::measureText/getTextSize)
static float s_estimateTextWidth(const std::string &text, float fontSize)
{
    return static_cast<float>(text.size()) * fontSize * 0.55f + 4.0f;
}

std::unique_ptr<ContextMenu::ItemData> AssetMenuItem::create(Rapture::AssetHandle asset)
{
    auto item = std::make_unique<AssetMenuItem>();
    item->assetHandle = asset;

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(asset);
    item->assetType = metadata.assetType;
    item->authoredClass = metadata.authoredClass;
    item->name = metadata.name;
    item->typeLabel = Asset_labelForType(metadata.assetType, metadata.authoredClass);

    return item;
}

Frame *AssetMenuItemView::create(ContextMenu &owner)
{
    Frame *row = ActionItemView::create(owner);

    const float thumbSize = AssetThumbnail::sizeForHeight(owner.itemHeight - 2.0f * THUMB_INSET);
    m_thumbnail.emplace(*row, thumbSize);
    m_thumbnail->getRoot()->setBaseProperties({
        .anchorPoint = {0.0f, 0.5f},
        .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
    });

    const float textLeft = ROW_PADDING + thumbSize + THUMB_GAP;
    const float blockHeight = MENU_NAME_HEIGHT + MENU_TYPE_HEIGHT;

    m_nameLabel = row->add<TextLabel>();
    m_nameLabel->setTextStyleProperties(owner.getTextStyleProperties());
    m_nameLabel->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_nameLabel->setBaseProperties({
        .interactable = false,
        .position = UDim2(0.0f, textLeft, 0.5f, -blockHeight * 0.5f),
        .size = UDim2(1.0f, -(textLeft + ROW_PADDING), 0.0f, MENU_NAME_HEIGHT),
    });

    m_typeLabel = row->add<TextLabel>();
    m_typeLabel->addClass(MENU_TYPE_CLASS);
    m_typeLabel->setBaseProperties({
        .interactable = false,
        .position = UDim2(0.0f, textLeft, 0.5f, -blockHeight * 0.5f + MENU_NAME_HEIGHT),
        .size = UDim2(1.0f, -(textLeft + ROW_PADDING), 0.0f, MENU_TYPE_HEIGHT),
    });

    return row;
}

void AssetMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &asset = item.as<AssetMenuItem>();

    if (asset.content) {
        m_thumbnail->getRoot()->setBaseProperties({.visible = false});
        m_nameLabel->setBaseProperties({.visible = false});
        m_typeLabel->setBaseProperties({.visible = false});
        asset.content(*m_row);
        return;
    }

    m_thumbnail->getRoot()->setBaseProperties({.visible = true});
    m_thumbnail->showAsset(asset.assetType, asset.authoredClass, asset.thumbnail);

    m_nameLabel->setBaseProperties({.visible = true});
    m_nameLabel->setText(asset.name);
    m_typeLabel->setBaseProperties({.visible = true});
    m_typeLabel->setText(asset.typeLabel);
}

std::unique_ptr<ContextMenu::ItemData> AddAssetMenuItem::createIconRow(std::string label, std::string svgIcon,
                                                                       std::function<void()> onActivate)
{
    auto item = std::make_unique<AddAssetMenuItem>();
    item->style = ADD_ROW_ICON;
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);
    item->onActivate = std::move(onActivate);

    return item;
}

std::unique_ptr<ContextMenu::ItemData> AddAssetMenuItem::createAssetRow(std::string label, Rapture::AssetType assetType,
                                                                        std::function<void()> onActivate)
{
    auto item = std::make_unique<AddAssetMenuItem>();
    item->style = ADD_ROW_ASSET;
    item->label = std::move(label);
    item->assetType = assetType;
    item->onActivate = std::move(onActivate);

    return item;
}

std::unique_ptr<ContextMenu::ItemData> AddAssetMenuItem::createPlainRow(std::string label, std::function<void()> onActivate)
{
    auto item = std::make_unique<AddAssetMenuItem>();
    item->style = ADD_ROW_PLAIN;
    item->label = std::move(label);
    item->onActivate = std::move(onActivate);

    return item;
}

float AddAssetMenuItemView::rowHeight(const ContextMenu &owner) const
{
    if (m_boundItem == nullptr || m_boundItem->as<AddAssetMenuItem>().style != ADD_ROW_ASSET) {
        return owner.itemHeight;
    }
    return ADD_ASSET_ROW_HEIGHT;
}

Frame *AddAssetMenuItemView::create(ContextMenu &owner)
{
    Frame *row = ActionItemView::create(owner);
    auto &addItem = m_boundItem->as<AddAssetMenuItem>();

    if (addItem.style == ADD_ROW_ASSET) {
        m_iconWell = row->add<Frame>();
        m_iconWell->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
        m_iconWell->setBaseProperties({
            .anchorPoint = {0.0f, 0.5f},
            .interactable = false,
            .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
            .size = UDim2::fromOffset(ADD_ASSET_WELL_SIZE, ADD_ASSET_WELL_SIZE),
        });

        m_icon = m_iconWell->add<ImageLabel>();
        m_icon->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
        m_icon->setBaseProperties({
            .anchorPoint = {0.5f, 0.5f},
            .interactable = false,
            .position = UDim2::fromScale(0.5f, 0.5f),
            .size = UDim2::fromOffset(ADD_ASSET_ICON_SIZE, ADD_ASSET_ICON_SIZE),
        });

        m_accent = m_iconWell->add<Frame>();
        m_accent->setBaseStyleProperties({.borderPixelSize = 0.0f});
        m_accent->setBaseProperties({
            .anchorPoint = {0.0f, 1.0f},
            .interactable = false,
            .position = UDim2::fromScale(0.0f, 1.0f),
            .size = UDim2(1.0f, 0.0f, 0.0f, ACCENT_HEIGHT),
        });
    } else if (addItem.style == ADD_ROW_ICON) {
        m_icon = row->add<ImageLabel>();
        m_icon->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
        m_icon->setBaseProperties({.anchorPoint = {0.0f, 0.5f}, .interactable = false});
    }

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({.interactable = false, .size = UDim2::fromScale(1.0f, 1.0f)});

    return row;
}

void AddAssetMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &addItem = item.as<AddAssetMenuItem>();

    float labelLeft = ROW_PADDING;
    if (addItem.style == ADD_ROW_ASSET) {
        m_icon->setSvg(Asset_iconForType(addItem.assetType, addItem.authoredClass));
        m_icon->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
        m_accent->setBaseStyleProperties({.backgroundColor = Asset_colorForType(addItem.assetType)});
        labelLeft += ADD_ASSET_WELL_SIZE + ICON_GAP;
    } else if (addItem.style == ADD_ROW_ICON) {
        float iconSize = m_owner->itemHeight - ICON_INSET;
        m_icon->setSvg(addItem.svgIcon);
        m_icon->setImageStyleProperties({.imageColor = m_owner->getTextStyleProperties().textColor});
        m_icon->setBaseProperties({
            .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
            .size = UDim2::fromOffset(iconSize, iconSize),
        });
        labelLeft += iconSize + ICON_GAP;
    }

    m_label->setBaseProperties({
        .padding =
            UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(ROW_PADDING), UDim::fromOffset(0.0f), UDim::fromOffset(labelLeft)},
    });
    m_label->setText(addItem.label);
    m_row->setBaseProperties({.interactable = addItem.enabled});
}

std::unique_ptr<ContextMenu::ItemData> ToggleMenuItem::create(std::string label, std::function<void(bool)> cb, std::string svgIcon)
{
    auto item = std::make_unique<ToggleMenuItem>(std::move(cb));
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);

    return item;
}

Frame *ToggleMenuItemView::create(ContextMenu &owner)
{
    Frame *row = ToggleItemView::create(owner);

    m_checkbox = row->add<Checkbox>();
    m_checkbox->addClass("menu-checkbox");
    m_checkbox->setBaseProperties({
        .anchorPoint = {0.0f, 0.5f},
        .interactable = false, // decorative only since the row handles the click
        .position = UDim2(0.0f, ROW_PADDING, 0.5f, 0.0f),
        .size = UDim2::fromOffset(MENU_CHECKBOX_SIZE, MENU_CHECKBOX_SIZE),
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

void ToggleMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &toggleItem = item.as<ToggleMenuItem>();

    float iconSize = m_owner->itemHeight - ICON_INSET;
    m_checkbox->value = &toggleItem.value;
    m_checkbox->markDirty();

    float labelLeft = ROW_PADDING + MENU_CHECKBOX_SIZE + ICON_GAP;
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

std::unique_ptr<ContextMenu::ItemData> RadioMenuItem::create(std::string label, RadioGroup *group, int32_t value,
                                                             std::string svgIcon)
{
    auto item = std::make_unique<RadioMenuItem>();
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);
    item->group = group;
    item->value = value;

    return item;
}

Frame *RadioMenuItemView::create(ContextMenu &owner)
{
    Frame *row = RadioItemView::create(owner);

    m_radio = row->add<RadioButton>();
    m_radio->setBaseProperties({
        .anchorPoint = {0.0f, 0.5f},
        .position = UDim2(0.0f, ROW_PADDING + (MENU_CHECKBOX_SIZE - MENU_RADIO_SIZE) * 0.5f, 0.5f, 0.0f),
        .size = UDim2::fromOffset(MENU_RADIO_SIZE, MENU_RADIO_SIZE),
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

void RadioMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &radioItem = item.as<RadioMenuItem>();

    float iconSize = m_owner->itemHeight - ICON_INSET;
    m_radio->value = radioItem.value;
    m_radio->setGroup(radioItem.group);

    float labelLeft = ROW_PADDING + MENU_CHECKBOX_SIZE + ICON_GAP;
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

std::unique_ptr<ContextMenu::ItemData> NumberMenuItem::create(std::string label, double value, std::function<void(double)> onChanged)
{
    auto item = std::make_unique<NumberMenuItem>();
    item->label = std::move(label);
    item->value = value;
    item->onChanged = std::move(onChanged);

    return item;
}

Frame *NumberMenuItemView::create(ContextMenu &owner)
{
    Frame *row = NumberItemView::create(owner);

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties(owner.getTextStyleProperties());
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({
        .interactable = false,
        .padding = UDim4{UDim::fromOffset(0.0f), UDim::fromOffset(ROW_PADDING), UDim::fromOffset(0.0f),
                         UDim::fromOffset(ROW_PADDING + MENU_CHECKBOX_SIZE + ICON_GAP)},
        .size = UDim2::fromScale(1.0f, 1.0f),
    });

    m_drag = row->add<DragFloat>();
    m_drag->addClass(NUMBER_FIELD_CLASS);
    m_drag->setBaseProperties({
        .anchorPoint = {1.0f, 0.5f},
        .position = UDim2(1.0f, -ROW_PADDING, 0.5f, 0.0f),
        .size = UDim2(NUMBER_FIELD_WIDTH_SCALE, 0.0f, 1.0f, -ICON_INSET),
    });

    return row;
}

void NumberMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &numberItem = item.as<NumberMenuItem>();

    m_label->setText(numberItem.label);
    m_row->setBaseProperties({.interactable = numberItem.enabled});

    m_drag->speed = numberItem.speed;
    m_drag->min = numberItem.min;
    m_drag->max = numberItem.max;
    if (!numberItem.format.empty()) {
        m_drag->setFormat(numberItem.format);
    }
    m_drag->value = &numberItem.value;
    m_drag->onValueChanged = [&numberItem](double value) {
        if (numberItem.onChanged != nullptr) {
            numberItem.onChanged(value);
        }
    };
    m_drag->markDirty();
}

std::unique_ptr<ContextMenu::ItemData> SeparatorMenuItem::create(std::string label)
{
    auto item = std::make_unique<SeparatorMenuItem>();
    item->label = std::move(label);

    return item;
}

Frame *SeparatorMenuItemView::create(ContextMenu &owner)
{
    Frame *row = SeparatorItemView::create(owner);
    row->setBaseProperties({
        .interactable = false,
        .padding = UDim4{.right = UDim::fromOffset(ROW_PADDING), .left = UDim::fromOffset(ROW_PADDING)},
    });
    // the line runs from the label, or from the start when there is none, to the end of the row
    auto *layout = row->addExtension<UIListLayout>();
    layout->fillDirection = FillDirection::FILL_HORIZONTAL;
    layout->horizontalFlex = UiFlexAlignment::FILL;
    layout->verticalAlignment = VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = UDim::fromOffset(SEPARATOR_GAP);

    m_label = row->add<TextLabel>();
    m_label->setTextStyleProperties({
        .fontSize = owner.getTextStyleProperties().fontSize,
        .textXAlignment = owner.getTextStyleProperties().textXAlignment,
        .textYAlignment = owner.getTextStyleProperties().textYAlignment,
    });
    m_label->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});
    m_label->setBaseProperties({
        .automaticSize = AutomaticSize::X,
        .interactable = false,
        .layoutOrder = 0,
        .size = UDim2::fromScale(0.0f, 1.0f),
    });

    m_line = row->add<Frame>();
    m_line->setBaseStyleProperties({.borderPixelSize = 0.0f});
    m_line->setBaseProperties({.interactable = false, .layoutOrder = 1, .size = UDim2::fromOffset(0.0f, 1.0f)});
    m_line->addExtension<UIFlexItem>();

    return row;
}

void SeparatorMenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &sep = item.as<SeparatorMenuItem>();

    m_label->setText(sep.label);
    m_label->setBaseProperties({.visible = !sep.label.empty()});
}

std::unique_ptr<ContextMenu::ItemData>
SubmenuItem::create(std::string label, std::vector<std::unique_ptr<ContextMenu::ItemData>> items, std::string svgIcon)
{
    auto item = std::make_unique<SubmenuItem>();
    item->label = std::move(label);
    item->svgIcon = std::move(svgIcon);
    item->items = std::move(items);

    return item;
}

Frame *SubmenuItemView::create(ContextMenu &owner)
{
    Frame *row = ContextMenu::SubmenuItemView::create(owner);

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

void SubmenuItemView::bind(ContextMenu::ItemData &item)
{
    m_boundItem = &item;
    auto &submenu = item.as<SubmenuItem>();

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
