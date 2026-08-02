#include "asset_picker.h"

#include "Icons.h"
#include "asset_manager/AssetManager.h"
#include "asset_visuals.h"
#include "context_menus.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <utility>

static constexpr float FACE_PADDING = 4.0f;
static constexpr float PREVIEW_SIZE = 18.0f;
static constexpr float PREVIEW_GAP = 6.0f;
static constexpr float ACCENT_HEIGHT = 2.0f;
static constexpr float ARROW_SIZE = 10.0f;
static constexpr float ARROW_GAP = 4.0f;
static constexpr float ARROW_OPEN_ROTATION = 180.0f;

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
    if (metadata.assetType == Rapture::AssetType::NONE) {
        return false;
    }
    if (metadata.isDiskAsset() && !query.includeDisk) {
        return false;
    }
    if (metadata.isVirtualAsset() && !query.includeVirtual) {
        return false;
    }
    if (!query.types.empty() && std::find(query.types.begin(), query.types.end(), metadata.assetType) == query.types.end()) {
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

std::vector<Rapture::AssetHandle> AssetQuery_collect(const AssetQuery &query)
{
    std::vector<ScoredAsset> scored;
    for (const auto &[handle, metadata] : Rapture::AssetManager::getAssetRegistry()) {
        if (metadata == nullptr) {
            continue;
        }
        if (!s_matches(query, handle, *metadata)) {
            continue;
        }
        scored.push_back({handle, s_nameScore(metadata->name, query.nameFilter), s_toLower(metadata->name)});
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

void AssetPicker::buildFace(Amethyst::UIScope &parent)
{
    parent.frame(
        {
            .classes = m_classes,
            .base = {.interactable = true, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &f) {
            m_root = &f.component;

            f.imageLabel(
                {
                    .classes = m_classes,
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .interactable = false,
                            .position = Amethyst::UDim2(0.0f, FACE_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(PREVIEW_SIZE, PREVIEW_SIZE),
                        },
                },
                [this](Amethyst::ImageLabelScope &il) {
                    m_preview = &il.component;
                    il.frame(
                        {
                            .base =
                                {
                                    .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
                                    .interactable = false,
                                    .position = Amethyst::UDim2::fromScale(0.0f, 1.0f),
                                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ACCENT_HEIGHT),
                                },
                            .style = {.backgroundTransparency = 0.0f, .borderPixelSize = 0.0f},
                        },
                        [this](Amethyst::FrameScope &accent) { m_typeAccent = &accent.component; });
                });

            float textLeft = FACE_PADDING + PREVIEW_SIZE + PREVIEW_GAP;
            float textRight = FACE_PADDING + ARROW_SIZE + ARROW_GAP;

            f.textLabel(
                {
                    .classes = m_classes,
                    .base =
                        {
                            .interactable = false,
                            .position = Amethyst::UDim2(0.0f, textLeft, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(textLeft + textRight), 1.0f, 0.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
                },
                [this](Amethyst::TextLabelScope &tl) { m_label = &tl.component; });

            f.imageLabel(
                {
                    .classes = m_classes,
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .interactable = false,
                            .position = Amethyst::UDim2(1.0f, -FACE_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(ARROW_SIZE, ARROW_SIZE),
                        },
                    .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
                    .svg = Icons::SVG_CARET_DOWN,
                },
                [this](Amethyst::ImageLabelScope &il) { m_arrow = &il.component; });

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
    m_menu->setRowFactories({.action = [] { return std::make_unique<AssetContextMenuAIV>(); }});
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
        auto item = AssetContextMenuAID::create(handle);
        item->as<AssetContextMenuAID>().onActivate = [this, handle]() { selectAsset(handle); };
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

void AssetPicker::applySelection()
{
    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(m_selected);

    m_label->setText(metadata.name);
    m_preview->setSvg(Asset_iconForType(metadata.assetType));
    m_typeAccent->setBaseStyleProperties({.backgroundColor = Asset_colorForType(metadata.assetType)});
}

void AssetPicker::applyOpenState(bool open)
{
    m_arrow->setBaseProperties({.rotation = open ? ARROW_OPEN_ROTATION : 0.0f});
}
