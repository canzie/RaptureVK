#include "AssetThumbnail.h"

#include "Icons.h"
#include "ui/AssetVisuals.h"

#include <cmath>
#include <string>
#include <string_view>

static constexpr float CHECKERBOARD_SQUARES_PER_SIDE = 8.0f;
static constexpr float TYPE_BAR_GAP = 3.0f;
static constexpr float TYPE_BAR_HEIGHT = 3.0f;
static constexpr float ICON_SHARE = 0.55f;
static constexpr float PLUS_SHARE = 0.45f;
static constexpr float STRIPES_SHARE = 1.0f;

static constexpr std::string_view BASE_CLASS = "asset-thumbnail";
static constexpr std::string_view EMPTY_BASE_CLASS = "asset-thumbnail-empty";
static constexpr std::string_view MISSING_BASE_CLASS = "asset-thumbnail-missing";
static constexpr std::string_view SQUARES_CLASS = "asset-thumbnail-squares";
static constexpr std::string_view PICTURE_CLASS = "asset-thumbnail-picture";
static constexpr std::string_view PLUS_PICTURE_CLASS = "asset-thumbnail-plus";
static constexpr std::string_view STRIPES_PICTURE_CLASS = "asset-thumbnail-stripes";

// white so the theme class tints it
static constexpr const char *STRIPES_SVG = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\">"
                                           "<path d=\"M-4 4 4-4M-4 12 12-4M0 16 16 0M4 20 20 4M12 20 20 12\" "
                                           "stroke=\"#FFFFFF\" stroke-width=\"2.2\"/></svg>";

static Amethyst::Frame *s_addSquaresLayer(Amethyst::Frame &base, float square, float offset)
{
    auto *layer = base.add<Amethyst::Frame>();
    layer->addClass(SQUARES_CLASS);
    layer->setBaseProperties({.interactable = false, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    layer->setBaseStyleProperties({
        .backgroundRepeat = Amethyst::BackgroundRepeat::REPEAT,
        .backgroundSize = Amethyst::vec2(square, square),
        .backgroundStep = Amethyst::vec2(2.0f * square, 2.0f * square),
        .backgroundPosition = Amethyst::vec2(offset, offset),
    });
    return layer;
}

AssetThumbnail::AssetThumbnail(Amethyst::UIObject &parent, float size) : m_size(size)
{
    m_root = parent.add<Amethyst::Frame>();
    m_root->setBaseProperties({.interactable = false, .size = Amethyst::UDim2::fromOffset(size, heightForSize(size))});
    m_root->setBaseStyleProperties({.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f});

    m_base = m_root->add<Amethyst::Frame>();
    m_base->addClass(BASE_CLASS);
    m_base->setBaseProperties({.clipsDescendants = true, .interactable = false, .size = Amethyst::UDim2::fromOffset(size, size)});

    const float square = size / CHECKERBOARD_SQUARES_PER_SIDE;
    m_squares = s_addSquaresLayer(*m_base, square, 0.0f);
    m_offsetSquares = s_addSquaresLayer(*m_base, square, square);

    m_picture = m_base->add<Amethyst::ImageLabel>();
    m_picture->addClass(PICTURE_CLASS);
    m_picture->setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
        .interactable = false,
        .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
    });

    m_typeBar = m_root->add<Amethyst::Frame>();
    m_typeBar->setBaseProperties({
        .interactable = false,
        .position = Amethyst::UDim2::fromOffset(0.0f, size + TYPE_BAR_GAP),
        .size = Amethyst::UDim2::fromOffset(size, TYPE_BAR_HEIGHT),
    });
    m_typeBar->setBaseStyleProperties({.backgroundTransparency = 0.0f, .borderPixelSize = 0.0f});
}

float AssetThumbnail::heightForSize(float size)
{
    return size + TYPE_BAR_GAP + TYPE_BAR_HEIGHT;
}

float AssetThumbnail::sizeForHeight(float height)
{
    const float room = height - TYPE_BAR_GAP - TYPE_BAR_HEIGHT;
    return std::floor(room / CHECKERBOARD_SQUARES_PER_SIDE) * CHECKERBOARD_SQUARES_PER_SIDE;
}

void AssetThumbnail::showAsset(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass, Amethyst::AmTextureId picture)
{
    applyBase("", true);
    if (picture.isValid()) {
        m_picture->setImage(picture);
        m_picture->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        m_picture->removeClass(PLUS_PICTURE_CLASS);
        m_picture->removeClass(STRIPES_PICTURE_CLASS);
    } else {
        applyPicture(Asset_iconForType(type, authoredClass), ICON_SHARE, "");
    }
    applyTypeBar(type);
}

void AssetThumbnail::showEmpty(Rapture::AssetType expectedType)
{
    applyBase(EMPTY_BASE_CLASS, false);
    applyPicture(Icons::SVG_PLUS, PLUS_SHARE, PLUS_PICTURE_CLASS);
    applyTypeBar(expectedType);
}

void AssetThumbnail::showMissing(Rapture::AssetType expectedType)
{
    applyBase(MISSING_BASE_CLASS, false);
    applyPicture(STRIPES_SVG, STRIPES_SHARE, STRIPES_PICTURE_CLASS);
    applyTypeBar(expectedType);
}

void AssetThumbnail::applyBase(std::string_view baseClass, bool checkered)
{
    m_base->removeClass(EMPTY_BASE_CLASS);
    m_base->removeClass(MISSING_BASE_CLASS);
    if (!baseClass.empty()) {
        m_base->addClass(baseClass);
    }

    m_squares->setBaseProperties({.visible = checkered});
    m_offsetSquares->setBaseProperties({.visible = checkered});
}

void AssetThumbnail::applyPicture(const char *svg, float share, std::string_view pictureClass)
{
    m_picture->setSvg(svg);
    m_picture->setBaseProperties({.size = Amethyst::UDim2::fromOffset(m_size * share, m_size * share)});

    m_picture->removeClass(PLUS_PICTURE_CLASS);
    m_picture->removeClass(STRIPES_PICTURE_CLASS);
    if (!pictureClass.empty()) {
        m_picture->addClass(pictureClass);
    }
}

void AssetThumbnail::applyTypeBar(Rapture::AssetType type)
{
    if (type == Rapture::ASSET_NONE) {
        m_typeBar->setBaseProperties({.visible = false});
        return;
    }

    m_typeBar->setBaseProperties({.visible = true});
    m_typeBar->setBaseStyleProperties({.backgroundColor = Asset_colorForType(type)});
}
