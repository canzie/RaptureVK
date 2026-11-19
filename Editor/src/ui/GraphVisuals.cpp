#include "GraphVisuals.h"

#include "Icons.h"

#include <array>

static constexpr float HEADER_FILL_START_STRENGTH = 0.55f;
static constexpr float HEADER_FILL_END_STRENGTH = 0.22f;
static constexpr float HEADER_FILL_ANGLE = 0.0f;

struct GraphCategoryVisual {
    std::string_view name;
    const char *iconSvg;
    Amethyst::Color3 color;
};

static const std::array<GraphCategoryVisual, GRAPH_CATEGORY_COUNT> CATEGORY_VISUALS = {{
    {"Input", Icons::SVG_GRID, Amethyst::Color3::fromRgb(192, 121, 74)},
    {"Texture", Icons::SVG_LAYERS, Amethyst::Color3::fromRgb(78, 125, 191)},
    {"Math", Icons::SVG_X, Amethyst::Color3::fromRgb(122, 122, 122)},
    {"Vector", Icons::SVG_TRANSFORM, Amethyst::Color3::fromRgb(96, 140, 170)},
    {"Color", Icons::SVG_LIGHT, Amethyst::Color3::fromRgb(176, 150, 72)},
    {"Output", Icons::SVG_MATERIAL, Amethyst::Color3::fromRgb(94, 158, 90)},
}};

// the header is drawn over this, so the faded category color is blended onto it up front
static const Amethyst::Color3 HEADER_BASE = Amethyst::Color3::fromHex(0x252525);

static Amethyst::Color3 s_blendOverHeaderBase(Amethyst::Color3 color, float strength)
{
    return Amethyst::Color3(HEADER_BASE.r + (color.r - HEADER_BASE.r) * strength, HEADER_BASE.g + (color.g - HEADER_BASE.g) * strength,
                            HEADER_BASE.b + (color.b - HEADER_BASE.b) * strength);
}

static std::array<Amethyst::Color3, GRAPH_CATEGORY_COUNT> s_buildHeaderFills()
{
    std::array<Amethyst::Color3, GRAPH_CATEGORY_COUNT> fills;
    for (size_t i = 0; i < fills.size(); ++i) {
        const Amethyst::Color3 color = CATEGORY_VISUALS[i].color;
        fills[i] = Amethyst::Color3::fromGradient(Amethyst::Gradient::linear(
            HEADER_FILL_ANGLE, {{0.0f, s_blendOverHeaderBase(color, HEADER_FILL_START_STRENGTH)},
                                {1.0f, s_blendOverHeaderBase(color, HEADER_FILL_END_STRENGTH)}}));
    }
    return fills;
}

std::string_view GraphCategory_name(GraphCategory category)
{
    return CATEGORY_VISUALS[category].name;
}

const char *GraphCategory_icon(GraphCategory category)
{
    return CATEGORY_VISUALS[category].iconSvg;
}

Amethyst::Color3 GraphCategory_color(GraphCategory category)
{
    return CATEGORY_VISUALS[category].color;
}

Amethyst::Color3 GraphCategory_headerFill(GraphCategory category)
{
    static const std::array<Amethyst::Color3, GRAPH_CATEGORY_COUNT> fills = s_buildHeaderFills();
    return fills[category];
}

Amethyst::Color3 PinType_color(Rapture::PinType type)
{
    switch (type) {
    case Rapture::PinType::FLOAT:
        return Amethyst::Color3::fromHex(0x9A9A9A);
    case Rapture::PinType::INT:
        return Amethyst::Color3::fromHex(0x4F9D55);
    case Rapture::PinType::VEC2:
        return Amethyst::Color3::fromHex(0x5BB8D0);
    case Rapture::PinType::VEC3:
        return Amethyst::Color3::fromHex(0x8178E0);
    case Rapture::PinType::VEC4:
        return Amethyst::Color3::fromHex(0xD8B13A);
    case Rapture::PinType::TEXTURE:
        return Amethyst::Color3::fromHex(0xC85BA8);
    }
    return Amethyst::Color3::fromHex(0x9A9A9A);
}
