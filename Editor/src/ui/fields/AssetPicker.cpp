#include "AssetPicker.h"

#include "Icons.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/IconButton.h"
#include "ui/menus/ContextMenuItems.h"
#include <rapture/assets/asset_manager/AssetManager.h>

#include <components/text_label.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

static constexpr float FACE_PADDING = 6.0f;
static constexpr float TEXT_GAP = 12.0f;
static constexpr float NAME_HEIGHT = 18.0f;
static constexpr float TYPE_HEIGHT = 15.0f;
static constexpr float NAME_TYPE_GAP = 2.0f;
static constexpr float REMOVE_BUTTON_SIZE = 24.0f;
static constexpr float REMOVE_ICON_SIZE = 13.0f;

static constexpr std::string_view FACE_CLASS = "asset-field";
static constexpr std::string_view NAME_CLASS = "asset-field-name";
static constexpr std::string_view MISSING_NAME_CLASS = "asset-field-name-missing";
static constexpr std::string_view TYPE_CLASS = "asset-field-type";
static constexpr std::string_view EXPECTED_TYPE_CLASS = "asset-field-type-expected";
static constexpr std::string_view REMOVE_BUTTON_CLASS = "asset-field-button";

// name match quality, lower sorts first
static constexpr int32_t SCORE_EXACT = 0;
static constexpr int32_t SCORE_PREFIX = 1;
static constexpr int32_t SCORE_SUBSTRING = 2;
static constexpr int32_t SCORE_NO_MATCH = 3;

struct ScoredAsset {
    Rapture::AssetHandle handle;
    int32_t score;
    std::string sortName;
};

static std::string s_toLower(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

static int32_t s_nameScore(std::string_view name, std::string_view filter)
{
    if (filter.empty()) {
        return SCORE_EXACT;
    }

    std::string loweredName = s_toLower(name);
    std::string loweredFilter = s_toLower(filter);

    size_t at = loweredName.find(loweredFilter);
    if (at == std::string::npos) {
        return SCORE_NO_MATCH;
    }
    if (loweredName.size() == loweredFilter.size()) {
        return SCORE_EXACT;
    }
    if (at == 0) {
        return SCORE_PREFIX;
    }
    return SCORE_SUBSTRING;
}

static bool s_matches(const AssetQuery &query, Rapture::AssetHandle handle, const Rapture::AssetMetadata &metadata)
{
    if (metadata.assetType == Rapture::ASSET_NONE) {
        return false;
    }
    if (metadata.isDiskAsset() && !query.includeDisk) {
        return false;
    }
    if (metadata.isVirtualAsset() && !query.includeVirtual) {
        return false;
    }
    if (s_nameScore(metadata.name, query.nameFilter) == SCORE_NO_MATCH) {
        return false;
    }
    if (query.predicate && !query.predicate(handle, metadata)) {
        return false;
    }
    return true;
}

static void s_collectType(const AssetQuery &query, Rapture::AssetType type, std::vector<ScoredAsset> &scored)
{
    for (const Rapture::AssetSlot &slot : Rapture::AssetManager::getAssetsOfType(type)) {
        const Rapture::AssetMetadata &metadata = *slot.metadata;
        if (!s_matches(query, slot.handle, metadata)) {
            continue;
        }
        scored.push_back({slot.handle, s_nameScore(metadata.name, query.nameFilter), s_toLower(metadata.name)});
    }
}

std::vector<Rapture::AssetHandle> AssetQuery_collect(const AssetQuery &query)
{
    std::vector<ScoredAsset> scored;
    if (query.types.empty()) {
        for (int type = 0; type < Rapture::ASSET_TYPE_COUNT; ++type) {
            s_collectType(query, static_cast<Rapture::AssetType>(type), scored);
        }
    } else {
        for (Rapture::AssetType type : query.types) {
            s_collectType(query, type, scored);
        }
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredAsset &a, const ScoredAsset &b) {
        if (a.score != b.score) {
            return a.score < b.score;
        }
        if (a.sortName != b.sortName) {
            return a.sortName < b.sortName;
        }
        return a.handle < b.handle;
    });

    std::vector<Rapture::AssetHandle> handles;
    handles.reserve(scored.size());
    for (const ScoredAsset &entry : scored) {
        handles.push_back(entry.handle);
    }
    return handles;
}

AssetPicker::AssetPicker(Amethyst::UIScope &parent, AssetPickerConfig config, std::vector<std::string> classes)
    : m_config(std::move(config)), m_classes(std::move(classes))
{
    buildFace(parent);
    applySelection();
}

void AssetPicker::setAsset(Rapture::AssetHandle handle)
{
    m_selected = handle;
    applySelection();
}

bool AssetPicker::isOpen() const
{
    return m_menu != nullptr && m_menu->isOpen();
}

void AssetPicker::open()
{
    if (isOpen()) {
        return;
    }

    buildMenu();
    m_menu->popupWidth = m_config.popupWidth > 0.0f ? m_config.popupWidth : m_root->absoluteSize.x;

    rebuildItems();
    m_menu->show(m_root);
    applyOpenState(true);
}

void AssetPicker::close()
{
    if (!isOpen()) {
        return;
    }
    m_menu->hide();
}

static void s_setGuiStateFlag(Amethyst::UIObject &object, uint16_t flag, bool on)
{
    const uint16_t state = object.getGuiState();
    object.setGuiState(on ? static_cast<uint16_t>(state | flag) : static_cast<uint16_t>(state & ~flag));
}

void AssetPicker::buildFace(Amethyst::UIScope &parent)
{
    parent.frame(
        {
            .classes = {std::string(FACE_CLASS)},
            .base = {.interactable = true, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &f) {
            m_root = &f.component;

            m_thumbnail.emplace(*m_root, m_config.previewSize);
            m_thumbnail->getRoot()->setBaseProperties({
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .position = Amethyst::UDim2(0.0f, FACE_PADDING, 0.5f, 0.0f),
            });

            buildNameAndType(f);
            buildRemoveButton();

            m_root->track(m_root->onHoverChanged.connect(
                [this](bool hovered) { s_setGuiStateFlag(*m_root, Amethyst::GUI_STATE_HOVERED, hovered); }));
            m_root->track(m_root->onInputBeganCb.connect([this](const Amethyst::InputObject &io) {
                if (io.type != Amethyst::InputType::MOUSE_BUTTON_1) {
                    return;
                }
                if (isOpen()) {
                    close();
                } else {
                    open();
                }
            }));
        });
}

void AssetPicker::buildNameAndType(Amethyst::FrameScope &face)
{
    const float textLeft = FACE_PADDING + m_config.previewSize + TEXT_GAP;
    const float textRight = m_config.allowsEmpty ? FACE_PADDING + REMOVE_BUTTON_SIZE + TEXT_GAP : FACE_PADDING;
    const float blockHeight = NAME_HEIGHT + NAME_TYPE_GAP + TYPE_HEIGHT;

    face.textLabel(
        {
            .classes = {std::string(NAME_CLASS)},
            .base =
                {
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, textLeft, 0.5f, -blockHeight * 0.5f),
                    .size = Amethyst::UDim2(1.0f, -(textLeft + textRight), 0.0f, NAME_HEIGHT),
                },
        },
        [this](Amethyst::TextLabelScope &tl) { m_nameLabel = &tl.component; });

    face.textLabel(
        {
            .classes = {std::string(TYPE_CLASS)},
            .base =
                {
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, textLeft, 0.5f, -blockHeight * 0.5f + NAME_HEIGHT + NAME_TYPE_GAP),
                    .size = Amethyst::UDim2(1.0f, -(textLeft + textRight), 0.0f, TYPE_HEIGHT),
                },
        },
        [this](Amethyst::TextLabelScope &tl) { m_typeLabel = &tl.component; });
}

void AssetPicker::buildRemoveButton()
{
    m_removeButton =
        m_root->add<IconButton>(Icons::SVG_X, REMOVE_ICON_SIZE, std::vector<std::string>{std::string(REMOVE_BUTTON_CLASS)});
    m_removeButton->setBaseProperties({
        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
        .position = Amethyst::UDim2(1.0f, -FACE_PADDING, 0.5f, 0.0f),
        .size = Amethyst::UDim2::fromOffset(REMOVE_BUTTON_SIZE, REMOVE_BUTTON_SIZE),
    });
    m_removeButton->track(
        m_removeButton->onMouseButton1ClickCb.connect([this]() { selectAsset(Rapture::INVALID_ASSET_HANDLE); }));
}

void AssetPicker::buildMenu()
{
    if (m_menu != nullptr) {
        return;
    }

    m_menu = m_root->add<Amethyst::ContextMenu>();
    for (const std::string &styleClass : m_classes) {
        m_menu->addClass(styleClass);
    }
    m_menu->itemHeight = m_config.rowHeight;
    m_menu->maxVisibleItems = m_config.maxVisibleRows;
    m_menu->setRowFactories({.action = [] { return std::make_unique<AssetMenuItemView>(); }});
    m_menu->onClosedCb = [this]() { applyOpenState(false); };
}

void AssetPicker::rebuildItems()
{
    AssetQuery query;
    query.types = m_config.types;
    query.predicate = m_config.predicate;

    std::vector<Rapture::AssetHandle> handles = AssetQuery_collect(query);

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.reserve(handles.size());
    for (Rapture::AssetHandle handle : handles) {
        auto item = AssetMenuItem::create(handle);
        item->as<AssetMenuItem>().onActivate = [this, handle]() { selectAsset(handle); };
        items.push_back(std::move(item));
    }

    m_menu->setItems(std::move(items));
}

void AssetPicker::selectAsset(Rapture::AssetHandle handle)
{
    setAsset(handle);
    if (onAssetSelected) {
        onAssetSelected(handle);
    }
}

Rapture::AssetType AssetPicker::expectedType() const
{
    return m_config.types.size() == 1 ? m_config.types.front() : Rapture::ASSET_NONE;
}

void AssetPicker::applySelection()
{
    const Rapture::AssetType expected = expectedType();
    const std::string expectedLabel = expected == Rapture::ASSET_NONE ? std::string() : Asset_labelForType(expected);

    m_nameLabel->removeClass(MISSING_NAME_CLASS);
    m_typeLabel->removeClass(EXPECTED_TYPE_CLASS);

    if (m_selected == Rapture::INVALID_ASSET_HANDLE) {
        m_thumbnail->showEmpty(expected);
        m_nameLabel->setBaseProperties({.visible = false});
        m_typeLabel->addClass(EXPECTED_TYPE_CLASS);
        m_typeLabel->setText(expectedLabel);
        m_removeButton->setBaseProperties({.visible = false});
        return;
    }

    m_nameLabel->setBaseProperties({.visible = true});
    m_removeButton->setBaseProperties({.visible = m_config.allowsEmpty});

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_selected);
    if (metadata.assetType == Rapture::ASSET_NONE) {
        // the asset is gone with its metadata, so its id is all that is left to name it by
        m_thumbnail->showMissing(expected);
        m_nameLabel->addClass(MISSING_NAME_CLASS);
        m_nameLabel->setText(std::format("{:016x}", m_selected));
        m_typeLabel->setText(expectedLabel);
        return;
    }

    m_thumbnail->showAsset(metadata.assetType, metadata.authoredClass);
    m_nameLabel->setText(metadata.name);
    m_typeLabel->setText(Asset_labelForType(metadata.assetType, metadata.authoredClass));
}

void AssetPicker::applyOpenState(bool open)
{
    s_setGuiStateFlag(*m_root, Amethyst::GUI_STATE_ACTIVE, open);
}
