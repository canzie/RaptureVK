#include "TextureCanvasPanel.h"

#include "Icons.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/menus/ContextMenuItems.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/gpu/textures/Texture.h>

#include <components/extensions/ui_list_layout.h>
#include <components/input_interface.h>

#include <algorithm>
#include <array>
#include <climits>
#include <string>

static constexpr float CANVAS_PADDING = 6.0f;
static constexpr float TOOLBAR_HEIGHT = 30.0f;
static constexpr float TOOLBAR_PADDING = 2.0f;
static constexpr float TOOLBAR_GAP = 2.0f;
static constexpr float TOOLBAR_INSET = 8.0f;
static constexpr float TOOLBAR_BUTTON_HEIGHT = TOOLBAR_HEIGHT - 2.0f * TOOLBAR_PADDING;
static constexpr float CHANNEL_BUTTON_WIDTH = TOOLBAR_BUTTON_HEIGHT;
static constexpr float SCALE_BUTTON_WIDTH = 36.0f;
static constexpr float DIVIDER_WIDTH = 1.0f;
static constexpr float DIVIDER_HEIGHT = 16.0f;
static constexpr float FIT_MARGIN = 2.0f * (TOOLBAR_INSET + TOOLBAR_HEIGHT);
static constexpr std::string_view TOOLBAR_CLASS = "viewport-toolbar";
static constexpr std::string_view TOOLBAR_BUTTON_CLASS = "viewport-toolbar-button";
static constexpr std::string_view TOOLBAR_DIVIDER_CLASS = "viewport-toolbar-divider";
static constexpr std::string_view CHANNEL_BUTTON_CLASS = "texture-channel-button";
static constexpr std::string_view MIP_THUMBNAIL_CLASS = "texture-mip-thumbnail";
static constexpr std::string_view MIP_LABEL_CLASS = "texture-mip-label";
static constexpr std::string_view TEXEL_READOUT_CLASS = "texture-texel-readout";

static constexpr uint32_t MIP_STRIP_ENTRY_COUNT = 7;
static constexpr float MIP_STRIP_PADDING_X = 6.0f;
static constexpr float MIP_STRIP_PADDING_Y = 8.0f;
static constexpr float MIP_STRIP_GAP = 8.0f;
static constexpr float MIP_ENTRY_GAP = 4.0f;
static constexpr float MIP_LABEL_HEIGHT = 12.0f;
static constexpr float MIP_THUMBNAIL_LARGEST = 64.0f;
static constexpr float MIP_THUMBNAIL_SHRINK = 9.0f;
static constexpr float MIP_THUMBNAIL_SMALLEST = 14.0f;
static constexpr float TEXEL_READOUT_PADDING = 8.0f;
static constexpr std::string_view CANVAS_CLASS = "texture-canvas";
static constexpr std::string_view CHECKERBOARD_CLASS = "texture-checkerboard";
static constexpr std::string_view CHECKERBOARD_SQUARES_CLASS = "texture-checkerboard-squares";
static constexpr std::string_view CHECKERBOARD_OFFSET_SQUARES_CLASS = "texture-checkerboard-offset-squares";

static constexpr uint32_t SHOWN_CHANNEL_COUNT = TextureCanvasPanel::SHOWN_CHANNEL_COUNT;
static constexpr uint8_t SHOWN_COLOR_CHANNEL_BITS = 0b0111;

static constexpr std::array<Rapture::TextureChannel, SHOWN_CHANNEL_COUNT> SHOWN_CHANNELS = {
    Rapture::TEXTURE_CHANNEL_R,
    Rapture::TEXTURE_CHANNEL_G,
    Rapture::TEXTURE_CHANNEL_B,
    Rapture::TEXTURE_CHANNEL_A,
};

static constexpr std::array<std::string_view, SHOWN_CHANNEL_COUNT> SHOWN_CHANNEL_LABELS = {"R", "G", "B", "A"};

static constexpr std::array<std::string_view, SHOWN_CHANNEL_COUNT> SHOWN_CHANNEL_CLASSES = {
    "texture-channel-red",
    "texture-channel-green",
    "texture-channel-blue",
    "texture-channel-alpha",
};

static constexpr std::array<std::string_view, TEXTURE_CANVAS_SCALE_COUNT> TEXTURE_CANVAS_SCALE_LABELS = {
    "Fit",
    "1:1",
};

static void s_setObjectGuiState(Amethyst::UIObject &object, Amethyst::GuiState state, bool on)
{
    const uint16_t current = object.getGuiState();
    const uint16_t bit = static_cast<uint16_t>(state);
    object.setGuiState(static_cast<uint16_t>(on ? (current | bit) : (current & ~bit)));
}

static Amethyst::Frame *s_addVerticalStack(Amethyst::UIScope &parent, Amethyst::BasePropertiesArgs base, float gap)
{
    Amethyst::Frame *stack = nullptr;
    parent.frame({.base = std::move(base), .style = {.backgroundTransparency = 1.0f}},
                 [&](Amethyst::FrameScope &f) { stack = &f.component; });

    auto *layout = stack->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_CENTER_H;
    layout->innerPadding = Amethyst::UDim::fromOffset(gap);
    return stack;
}

static Amethyst::TextButton *s_addToolbarTextButton(Amethyst::Frame &toolbar, std::string_view label,
                                                    std::vector<std::string> classes, float width, uint32_t layoutOrder)
{
    Amethyst::TextButton *button = nullptr;
    Amethyst::UIScope(toolbar).textButton(
        {
            .classes = std::move(classes),
            .base = {.layoutOrder = layoutOrder, .size = Amethyst::UDim2::fromOffset(width, TOOLBAR_BUTTON_HEIGHT)},
            .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = std::string(label),
        },
        [&](Amethyst::TextButtonScope &b) { button = &b.component; });
    return button;
}

static uint8_t s_channelBit(Rapture::TextureChannel channel)
{
    return static_cast<uint8_t>(1u << channel);
}

static uint32_t s_registeredViewKey(uint32_t mip, uint8_t shownChannelBits)
{
    return (mip << SHOWN_CHANNEL_COUNT) | shownChannelBits;
}

static uint32_t s_mipExtent(uint32_t extent, uint32_t mip)
{
    return std::max(1u, extent >> mip);
}

static Rapture::TextureSwizzle s_swizzleForShownChannels(uint8_t shownChannelBits)
{
    for (Rapture::TextureChannel channel : SHOWN_CHANNELS) {
        if (shownChannelBits == s_channelBit(channel)) {
            return {.r = channel, .g = channel, .b = channel, .a = Rapture::TEXTURE_CHANNEL_ONE};
        }
    }

    auto shownOr = [shownChannelBits](Rapture::TextureChannel channel, Rapture::TextureChannel hidden) {
        return (shownChannelBits & s_channelBit(channel)) != 0 ? channel : hidden;
    };

    return {
        .r = shownOr(Rapture::TEXTURE_CHANNEL_R, Rapture::TEXTURE_CHANNEL_ZERO),
        .g = shownOr(Rapture::TEXTURE_CHANNEL_G, Rapture::TEXTURE_CHANNEL_ZERO),
        .b = shownOr(Rapture::TEXTURE_CHANNEL_B, Rapture::TEXTURE_CHANNEL_ZERO),
        .a = shownOr(Rapture::TEXTURE_CHANNEL_A, Rapture::TEXTURE_CHANNEL_ONE),
    };
}

static std::string s_mipLabel(const Rapture::TextureSpecification &spec, uint32_t mip)
{
    return "Mip " + std::to_string(mip) + ", " + std::to_string(s_mipExtent(spec.width, mip)) + " x " +
           std::to_string(s_mipExtent(spec.height, mip));
}

static Rapture::TextureSpecification s_nearestSamplerSpec()
{
    Rapture::TextureSpecification spec{};
    spec.filter = Rapture::TextureFilter::Nearest;
    spec.wrap = Rapture::TextureWrap::Repeat;
    return spec;
}

TextureCanvasPanel::TextureCanvasPanel(const WorkspaceContext &context, Rapture::AssetHandle handle)
    : Panel("Texture", context), m_texture(Rapture::AssetManager::getAsset<Rapture::ATexture>(handle)),
      m_nearestSampler(std::make_unique<Rapture::Sampler>(s_nearestSamplerSpec())), m_shownChannelBits(SHOWN_COLOR_CHANNEL_BITS)
{
    m_icon = Icons::SVG_LAYERS;
}

TextureCanvasPanel::~TextureCanvasPanel()
{
    for (const auto &[key, registered] : m_registeredViews) {
        m_context.editor->textures->unregisterTexture(registered.id);
    }
}

void TextureCanvasPanel::build(Amethyst::UIScope &scope)
{
    m_root->setBaseProperties({.clipsDescendants = true, .padding = Amethyst::UDim4::fromOffset(CANVAS_PADDING)});

    scope.frame(
        {
            .classes = {std::string(CANVAS_CLASS)},
            .base = {.clipsDescendants = true, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &canvas) {
            m_canvas = &canvas.component;
            canvas.frame(
                {
                    .classes = {std::string(CHECKERBOARD_CLASS)},
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
                            .interactable = false,
                            .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                        },
                },
                [this](Amethyst::FrameScope &checkerboard) { buildCheckerboardAndImage(checkerboard); });
        });

    if (!m_texture) {
        return;
    }

    buildToolbar();
    buildMipStrip();
    buildTexelReadout();

    m_textureWasReady = m_texture->isReady();
    if (m_textureWasReady) {
        showTexture();
    }
}

void TextureCanvasPanel::buildCheckerboardAndImage(Amethyst::FrameScope &checkerboard)
{
    m_checkerboard = &checkerboard.component;

    for (std::string_view squaresClass : {CHECKERBOARD_SQUARES_CLASS, CHECKERBOARD_OFFSET_SQUARES_CLASS}) {
        checkerboard.frame({
            .classes = {std::string(squaresClass)},
            .base = {.interactable = false, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        });
    }

    checkerboard.imageLabel(
        {
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f), .zIndex = 1},
            .style = {.backgroundTransparency = 1.0f},
            .image = {.imageColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f)},
        },
        [this](Amethyst::ImageLabelScope &image) {
            m_image = &image.component;
            m_image->track(m_image->onHoverChanged.connect([this](bool hovered) { onImageHoverChanged(hovered); }));
            m_image->track(
                m_image->onInputChangedCb.connect([this](const Amethyst::InputObject &input) { onImageCursorMoved(input); }));
        });
}

void TextureCanvasPanel::buildMipStrip()
{
    const Rapture::TextureSpecification &spec = m_texture->getSpecification();
    if (spec.mipLevels <= 1) {
        return;
    }

    Amethyst::UIScope canvas(*m_canvas);
    canvas.frame(
        {
            .classes = {std::string(TOOLBAR_CLASS)},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(1.0f, 0.0f),
                    .automaticSize = Amethyst::AutomaticSize::XY,
                    .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(MIP_STRIP_PADDING_Y),
                                               .right = Amethyst::UDim::fromOffset(MIP_STRIP_PADDING_X),
                                               .bottom = Amethyst::UDim::fromOffset(MIP_STRIP_PADDING_Y),
                                               .left = Amethyst::UDim::fromOffset(MIP_STRIP_PADDING_X)},
                    .position = Amethyst::UDim2(1.0f, -TOOLBAR_INSET, 0.0f, 2.0f * TOOLBAR_INSET + TOOLBAR_HEIGHT),
                    .zIndex = 2,
                },
        },
        [&](Amethyst::FrameScope &strip) {
            auto *layout = strip.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
            layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_CENTER_H;
            layout->innerPadding = Amethyst::UDim::fromOffset(MIP_STRIP_GAP);

            const uint32_t entryCount = std::min(spec.mipLevels, MIP_STRIP_ENTRY_COUNT);
            const float aspect = static_cast<float>(spec.height) / static_cast<float>(spec.width);
            for (uint32_t mip = 0; mip < entryCount; ++mip) {
                buildMipStripEntry(strip, mip, aspect);
            }

            if (spec.mipLevels > entryCount) {
                strip.textLabel({
                    .classes = {std::string(MIP_LABEL_CLASS)},
                    .base = {.automaticSize = Amethyst::AutomaticSize::X,
                             .layoutOrder = entryCount,
                             .size = Amethyst::UDim2::fromOffset(0.0f, MIP_LABEL_HEIGHT)},
                    .label = "+" + std::to_string(spec.mipLevels - entryCount),
                });
            }
        });

    showMipStripState();
}

void TextureCanvasPanel::buildMipStripEntry(Amethyst::UIScope &strip, uint32_t mip, float aspect)
{
    const float width = std::max(MIP_THUMBNAIL_SMALLEST, MIP_THUMBNAIL_LARGEST - static_cast<float>(mip) * MIP_THUMBNAIL_SHRINK);
    MipStripEntry &entry = m_mipStripEntries.emplace_back();

    Amethyst::Frame *stack =
        s_addVerticalStack(strip, {.automaticSize = Amethyst::AutomaticSize::XY, .layoutOrder = mip}, MIP_ENTRY_GAP);
    Amethyst::UIScope entryScope(*stack);

    entryScope.imageButton(
        {
            .classes = {std::string(MIP_THUMBNAIL_CLASS)},
            .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(width, width * aspect)},
            .image = {.imageColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f)},
        },
        [&](Amethyst::ImageButtonScope &b) {
            entry.thumbnail = &b.component;
            b.component.track(b.component.onMouseButton1ClickCb.connect([this, mip]() { m_mipGroup.select(static_cast<int32_t>(mip)); }));
        });

    entryScope.textLabel(
        {
            .classes = {std::string(MIP_LABEL_CLASS)},
            .base = {.automaticSize = Amethyst::AutomaticSize::X,
                     .layoutOrder = 1,
                     .size = Amethyst::UDim2::fromOffset(0.0f, MIP_LABEL_HEIGHT)},
            .label = std::to_string(mip),
        },
        [&](Amethyst::TextLabelScope &l) { entry.label = &l.component; });
}

void TextureCanvasPanel::buildTexelReadout()
{
    Amethyst::UIScope(*m_canvas)
        .frame(
            {
                .classes = {std::string(TOOLBAR_CLASS)},
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
                        .automaticSize = Amethyst::AutomaticSize::X,
                        .padding = Amethyst::UDim4::fromOffset(TOOLBAR_PADDING),
                        .position = Amethyst::UDim2(0.0f, TOOLBAR_INSET, 1.0f, -TOOLBAR_INSET),
                        .size = Amethyst::UDim2::fromOffset(0.0f, TOOLBAR_HEIGHT),
                        .visible = false,
                        .zIndex = 2,
                    },
            },
            [this](Amethyst::FrameScope &island) {
                m_texelReadoutIsland = &island.component;
                island.textLabel(
                    {
                        .classes = {std::string(TEXEL_READOUT_CLASS)},
                        .base = {.automaticSize = Amethyst::AutomaticSize::X,
                                 .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(TEXEL_READOUT_PADDING),
                                                            .left = Amethyst::UDim::fromOffset(TEXEL_READOUT_PADDING)},
                                 .size = Amethyst::UDim2(0.0f, 0.0f, 1.0f, 0.0f)},
                    },
                    [this](Amethyst::TextLabelScope &l) { m_texelReadout = &l.component; });
            });
}

void TextureCanvasPanel::buildToolbar()
{
    auto addToolbar = [this](Amethyst::vec2 anchor) {
        Amethyst::Frame *toolbar = nullptr;
        Amethyst::UIScope(*m_canvas)
            .frame(
                {
                    .classes = {std::string(TOOLBAR_CLASS)},
                    .base =
                        {
                            .anchorPoint = anchor,
                            .automaticSize = Amethyst::AutomaticSize::X,
                            .padding = Amethyst::UDim4::fromOffset(TOOLBAR_PADDING),
                            .position = Amethyst::UDim2(anchor.x, TOOLBAR_INSET * (1.0f - 2.0f * anchor.x), 0.0f, TOOLBAR_INSET),
                            .size = Amethyst::UDim2::fromOffset(0.0f, TOOLBAR_HEIGHT),
                            .zIndex = 2,
                        },
                },
                [&](Amethyst::FrameScope &f) { toolbar = &f.component; });

        auto *layout = toolbar->addExtension<Amethyst::UIListLayout>();
        layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
        layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
        layout->innerPadding = Amethyst::UDim::fromOffset(TOOLBAR_GAP);
        return toolbar;
    };

    Amethyst::Frame *viewToolbar = addToolbar(Amethyst::vec2(0.0f, 0.0f));
    buildChannelToggles(*viewToolbar);
    buildMipMenu(*viewToolbar);

    Amethyst::Frame *scaleToolbar = addToolbar(Amethyst::vec2(1.0f, 0.0f));
    buildScaleToggles(*scaleToolbar);
}

void TextureCanvasPanel::buildChannelToggles(Amethyst::Frame &toolbar)
{
    for (uint32_t i = 0; i < SHOWN_CHANNEL_COUNT; ++i) {
        const Rapture::TextureChannel channel = SHOWN_CHANNELS[i];
        Amethyst::TextButton *button =
            s_addToolbarTextButton(toolbar, SHOWN_CHANNEL_LABELS[i],
                                   {std::string(CHANNEL_BUTTON_CLASS), std::string(SHOWN_CHANNEL_CLASSES[i])}, CHANNEL_BUTTON_WIDTH, i);
        button->track(button->onMouseButton1ClickCb.connect(
            [this, channel]() { setChannelShown(channel, (m_shownChannelBits & s_channelBit(channel)) == 0); }));
        m_channelButtons[i] = button;
    }

    showChannelButtonStates();
}

void TextureCanvasPanel::buildMipMenu(Amethyst::Frame &toolbar)
{
    const Rapture::TextureSpecification &spec = m_texture->getSpecification();

    Amethyst::UIScope(toolbar).frame({
        .classes = {std::string(TOOLBAR_DIVIDER_CLASS)},
        .base = {.layoutOrder = SHOWN_CHANNEL_COUNT, .size = Amethyst::UDim2::fromOffset(DIVIDER_WIDTH, DIVIDER_HEIGHT)},
    });

    m_mipButton = toolbar.add<IconLabelButton>(Icons::SVG_LAYERS, s_mipLabel(spec, m_shownMip),
                                               std::vector<std::string>{std::string(TOOLBAR_BUTTON_CLASS)});
    m_mipButton->setBaseProperties(
        {.layoutOrder = SHOWN_CHANNEL_COUNT + 1, .size = Amethyst::UDim2::fromOffset(0.0f, TOOLBAR_BUTTON_HEIGHT)});
    m_mipButton->track(m_mipButton->onMouseButton1ClickCb.connect([this]() {
        const float buttonBottom = m_mipButton->absolutePosition.y + m_mipButton->absoluteSize.y;
        const float panelBottom = m_root->absolutePosition.y + m_root->absoluteSize.y;
        m_mipMenu->maxContentHeight = std::max(0.0f, panelBottom - buttonBottom);
        m_mipMenu->show(m_mipButton);
    }));

    m_mipMenu = m_root->add<Amethyst::ContextMenu>();
    m_mipMenu->addClass("viewport-context-menu");
    m_mipMenu->maxVisibleItems = INT_MAX;
    m_mipMenu->setRowFactories({.radio = [] { return std::make_unique<RadioMenuItemView>(); }});

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.reserve(spec.mipLevels);
    for (uint32_t mip = 0; mip < spec.mipLevels; ++mip) {
        items.push_back(RadioMenuItem::create(s_mipLabel(spec, mip), &m_mipGroup, static_cast<int32_t>(mip)));
    }
    m_mipMenu->setItems(std::move(items));

    m_mipGroup.value = static_cast<int32_t>(m_shownMip);
    m_mipGroupConn = m_mipGroup.onChanged.connect([this]() { setShownMip(static_cast<uint32_t>(m_mipGroup.value)); });
}

void TextureCanvasPanel::buildScaleToggles(Amethyst::Frame &toolbar)
{
    for (uint32_t i = 0; i < TEXTURE_CANVAS_SCALE_COUNT; ++i) {
        const auto scale = static_cast<TextureCanvasScale>(i);
        Amethyst::TextButton *button =
            s_addToolbarTextButton(toolbar, TEXTURE_CANVAS_SCALE_LABELS[i], {std::string(TOOLBAR_BUTTON_CLASS)}, SCALE_BUTTON_WIDTH, i);
        button->track(button->onMouseButton1ClickCb.connect([this, scale]() { setScale(scale); }));
        m_scaleButtons[i] = button;
    }

    showScaleButtonStates();
}

void TextureCanvasPanel::showChannelButtonStates()
{
    for (uint32_t i = 0; i < SHOWN_CHANNEL_COUNT; ++i) {
        if (m_channelButtons[i] != nullptr) {
            s_setObjectGuiState(*m_channelButtons[i], Amethyst::GUI_STATE_ACTIVE, (m_shownChannelBits & s_channelBit(SHOWN_CHANNELS[i])) != 0);
        }
    }
}

void TextureCanvasPanel::showScaleButtonStates()
{
    for (uint32_t i = 0; i < TEXTURE_CANVAS_SCALE_COUNT; ++i) {
        if (m_scaleButtons[i] != nullptr) {
            s_setObjectGuiState(*m_scaleButtons[i], Amethyst::GUI_STATE_ACTIVE, static_cast<TextureCanvasScale>(i) == m_scale);
        }
    }
}

void TextureCanvasPanel::setScale(TextureCanvasScale scale)
{
    m_scale = scale;
    showScaleButtonStates();
    layoutImage();
}

void TextureCanvasPanel::onRootDestroyed()
{
    m_canvas = nullptr;
    m_checkerboard = nullptr;
    m_image = nullptr;
    m_mipButton = nullptr;
    m_mipMenu = nullptr;
    m_channelButtons.fill(nullptr);
    m_scaleButtons.fill(nullptr);
    m_mipStripEntries.clear();
    m_texelReadoutIsland = nullptr;
    m_texelReadout = nullptr;
}

void TextureCanvasPanel::onUpdate(float dt)
{
    (void)dt;

    if (m_canvas == nullptr || !m_texture) {
        return;
    }

    // the asset's pixels may still be uploading when the workspace opens
    if (!m_textureWasReady && m_texture->isReady()) {
        m_textureWasReady = true;
        showTexture();
    }

    if (m_canvas->absoluteSize != m_laidOutCanvasSize) {
        layoutImage();
    }
}

void TextureCanvasPanel::setShownMip(uint32_t mip)
{
    m_shownMip = mip;
    if (m_mipButton != nullptr) {
        m_mipButton->getLabel().setText(s_mipLabel(m_texture->getSpecification(), mip));
    }
    showMipStripState();
    showSelectedView();
}

void TextureCanvasPanel::setChannelShown(Rapture::TextureChannel channel, bool shown)
{
    if (shown) {
        m_shownChannelBits |= s_channelBit(channel);
    } else {
        m_shownChannelBits &= static_cast<uint8_t>(~s_channelBit(channel));
    }
    showChannelButtonStates();
    showSelectedView();
}

void TextureCanvasPanel::showTexture()
{
    for (uint32_t mip = 0; mip < m_mipStripEntries.size(); ++mip) {
        if (m_mipStripEntries[mip].thumbnail != nullptr) {
            m_mipStripEntries[mip].thumbnail->setImage(registeredViewId(mip, SHOWN_COLOR_CHANNEL_BITS));
        }
    }
    showSelectedView();
}

void TextureCanvasPanel::showSelectedView()
{
    if (m_image == nullptr || !m_textureWasReady) {
        return;
    }

    m_image->setImage(registeredViewId(m_shownMip, m_shownChannelBits));
    layoutImage();
}

Amethyst::AmTextureId TextureCanvasPanel::registeredViewId(uint32_t mip, uint8_t shownChannelBits)
{
    const uint32_t key = s_registeredViewKey(mip, shownChannelBits);
    auto it = m_registeredViews.find(key);
    if (it == m_registeredViews.end()) {
        auto view = std::make_unique<Rapture::TextureView>(
            m_texture.get()->texture(),
            Rapture::TextureViewConfig{.baseMip = mip, .mipCount = 1, .swizzle = s_swizzleForShownChannels(shownChannelBits)});
        Amethyst::AmTextureId id = m_context.editor->textures->registerTextureView(*view, *m_nearestSampler);
        it = m_registeredViews.emplace(key, RegisteredTextureView{.view = std::move(view), .id = id}).first;
    }
    return it->second.id;
}

void TextureCanvasPanel::showMipStripState()
{
    for (uint32_t mip = 0; mip < m_mipStripEntries.size(); ++mip) {
        const bool shown = mip == m_shownMip;
        MipStripEntry &entry = m_mipStripEntries[mip];
        if (entry.thumbnail != nullptr) {
            s_setObjectGuiState(*entry.thumbnail, Amethyst::GUI_STATE_ACTIVE, shown);
        }
        if (entry.label != nullptr) {
            s_setObjectGuiState(*entry.label, Amethyst::GUI_STATE_ACTIVE, shown);
        }
    }
}

void TextureCanvasPanel::onImageHoverChanged(bool hovered)
{
    Amethyst::InputInterface::setCursorShape(hovered ? Amethyst::CURSOR_CROSSHAIR : Amethyst::CURSOR_ARROW);
    if (m_texelReadoutIsland != nullptr) {
        m_texelReadoutIsland->setBaseProperties({.visible = hovered});
    }
}

void TextureCanvasPanel::onImageCursorMoved(const Amethyst::InputObject &input)
{
    if (input.type != Amethyst::InputType::MOUSE_MOVEMENT || m_image == nullptr || m_texelReadout == nullptr) {
        return;
    }

    const Amethyst::vec2 imageSize = m_image->absoluteSize;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
        return;
    }

    const Rapture::TextureSpecification &spec = m_texture->getSpecification();
    const uint32_t mipWidth = s_mipExtent(spec.width, m_shownMip);
    const uint32_t mipHeight = s_mipExtent(spec.height, m_shownMip);
    const float u = (input.position.x - m_image->absolutePosition.x) / imageSize.x;
    const float v = (input.position.y - m_image->absolutePosition.y) / imageSize.y;
    const uint32_t x = std::min(static_cast<uint32_t>(std::max(u, 0.0f) * static_cast<float>(mipWidth)), mipWidth - 1);
    const uint32_t y = std::min(static_cast<uint32_t>(std::max(v, 0.0f) * static_cast<float>(mipHeight)), mipHeight - 1);

    m_texelReadout->setText(std::to_string(x) + ", " + std::to_string(y));
}

void TextureCanvasPanel::layoutImage()
{
    if (m_canvas == nullptr || m_checkerboard == nullptr || !m_texture) {
        return;
    }

    m_laidOutCanvasSize = m_canvas->absoluteSize;

    const Rapture::TextureSpecification &spec = m_texture->getSpecification();
    Amethyst::vec2 size;
    if (m_scale == TEXTURE_CANVAS_SCALE_ACTUAL_SIZE) {
        size = Amethyst::vec2(static_cast<float>(s_mipExtent(spec.width, m_shownMip)),
                              static_cast<float>(s_mipExtent(spec.height, m_shownMip)));
    } else {
        const float width = static_cast<float>(spec.width);
        const float height = static_cast<float>(spec.height);
        const float fit = std::min((m_laidOutCanvasSize.x - FIT_MARGIN) / width, (m_laidOutCanvasSize.y - FIT_MARGIN) / height);
        size = Amethyst::vec2(width * std::max(fit, 0.0f), height * std::max(fit, 0.0f));
    }

    m_checkerboard->setBaseProperties({.size = Amethyst::UDim2::fromOffset(size.x, size.y)});
}
