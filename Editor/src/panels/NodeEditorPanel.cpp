#include "NodeEditorPanel.h"

#include "Icons.h"
#include "ShortcutRegistry.h"
#include "ui/AssetVisuals.h"
#include "ui/ControlTooltip.h"
#include "ui/controls/IconButton.h"
#include "ui/controls/SearchField.h"

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/materials/AMaterialInstance.h>
#include <rapture/assets/materials/Material.h>
#include <rapture/assets/materials/MaterialInstance.h>
#include <rapture/assets/materials/graph/MaterialGraphCompiler.h>
#include <rapture/assets/materials/graph/NodeRegistry.h>
#include <rapture/assets/materials/graph/SurfaceGraphManager.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/textures/Texture.h>
#include <rapture/renderer/generators/textures/ProceduralTextures.h>

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/drag.h>
#include <components/dropdown.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_list_layout.h>
#include <components/image_label.h>
#include <components/popup.h>
#include <components/scrolling_frame.h>
#include <components/spline.h>
#include <components/text_button.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <utility>

static constexpr float NODE_WIDTH = 190.0f;
static constexpr float NODE_HEADER_HEIGHT = 28.0f;
static constexpr float NODE_HEADER_PADDING = 10.0f;
static constexpr float NODE_HEADER_GAP = 8.0f;
static constexpr float NODE_ICON_SIZE = 13.0f;
static constexpr float NODE_BODY_PADDING_TOP = 4.0f;
static constexpr float NODE_BODY_PADDING_BOTTOM = 6.0f;
static constexpr float NODE_ROW_HEIGHT = 22.0f;
static constexpr float NODE_VARIANT_ROW_HEIGHT = 28.0f;
static constexpr float NODE_ROW_INSET = 12.0f;
static constexpr float NODE_FIELD_INSET = 10.0f;
static constexpr float NODE_FIELD_WIDTH = 90.0f;
static constexpr float NODE_FIELD_HEIGHT = 18.0f;
static constexpr float NODE_AXIS_LABEL_WIDTH = 12.0f;
static constexpr float NODE_AXIS_FIELD_GAP = 6.0f;
static constexpr float NODE_AXIS_BAR_WIDTH = 3.0f;
static constexpr float NODE_PREVIEW_INSET = 10.0f;
static constexpr float PIN_SOCKET_SIZE = 12.0f;
static constexpr float PIN_SOCKET_TARGET_SIZE = 16.0f;
static constexpr float PIN_HIT_RADIUS = 11.0f;
static constexpr float WIRE_BEND = 70.0f;

static constexpr float CANVAS_INSET = 6.0f;
static constexpr Amethyst::vec2 INITIAL_PAN = {40.0f, 40.0f};
static constexpr float OVERLAY_MARGIN = 12.0f;
static constexpr float OVERLAY_HEIGHT = 30.0f;
static constexpr float OVERLAY_PADDING = 2.0f;
static constexpr float OVERLAY_GAP = 2.0f;
static constexpr float OVERLAY_BUTTON_SIZE = 26.0f;
static constexpr float OVERLAY_ICON_SIZE = 14.0f;
static constexpr float CRUMB_PADDING = 8.0f;
static constexpr float CRUMB_ICON_SIZE = 14.0f;
static constexpr float CRUMB_SEPARATOR_SIZE = 9.0f;
static constexpr int32_t OVERLAY_Z_INDEX = 101;

static constexpr float ADD_MENU_WIDTH = 330.0f;
static constexpr float ADD_MENU_PADDING = 6.0f;
static constexpr float ADD_MENU_GAP = 4.0f;
static constexpr float ADD_MENU_SEARCH_HEIGHT = 26.0f;
static constexpr float ADD_MENU_LIST_HEIGHT = 320.0f;
static constexpr Amethyst::vec2 ADD_MENU_CURSOR_OFFSET = {6.0f, 6.0f};
static constexpr float ADD_SECTION_HEIGHT = 24.0f;
static constexpr float ADD_SECTION_PADDING = 6.0f;
static constexpr float ADD_ENTRY_HEIGHT = 28.0f;
static constexpr float ADD_ENTRY_PADDING = 8.0f;
static constexpr float ADD_ENTRY_GAP = 8.0f;
static constexpr float ADD_SWATCH_SIZE = 8.0f;

static constexpr std::string_view SURFACE_CRUMB = "Surface";
static constexpr std::array<const char *, 4> AXIS_NAMES = {"X", "Y", "Z", "W"};
static constexpr std::array<std::string_view, 3> AXIS_BAR_CLASSES = {"axis-bar-x", "axis-bar-y", "axis-bar-z"};

static constexpr ControlTooltip FRAME_ALL_TOOLTIP = {.name = "Frame All", .description = "Moves the view so every node is in sight"};

using GNT = Rapture::GraphNodeType;
using TextureNodeKind = NodeEditorPanel::TextureNodeKind;

/**
 * @brief A node the add node menu offers
 */
struct NodeCatalogEntry {
    std::string_view name;
    GraphCategory category;
    Rapture::GraphNodeType type;
    TextureNodeKind textureKind;
    std::string_view description;
};

static constexpr NodeCatalogEntry NODE_CATALOG[] = {
    {"Float", GRAPH_CATEGORY_INPUT, GNT::CONSTANT_FLOAT, TextureNodeKind::NONE, "A single number"},
    {"Integer", GRAPH_CATEGORY_INPUT, GNT::CONSTANT_INT, TextureNodeKind::NONE, "A single whole number"},
    {"Vector 2", GRAPH_CATEGORY_INPUT, GNT::CONSTANT_VEC2, TextureNodeKind::NONE, "Two numbers"},
    {"Vector 3", GRAPH_CATEGORY_INPUT, GNT::CONSTANT_VEC3, TextureNodeKind::NONE, "Three numbers"},
    {"Vector 4", GRAPH_CATEGORY_INPUT, GNT::CONSTANT_VEC4, TextureNodeKind::NONE, "Four numbers"},
    {"Position", GRAPH_CATEGORY_INPUT, GNT::POSITION, TextureNodeKind::NONE, "The position of the shaded point"},
    {"Normal", GRAPH_CATEGORY_INPUT, GNT::NORMAL, TextureNodeKind::NONE, "The surface normal at the shaded point"},
    {"Tangent", GRAPH_CATEGORY_INPUT, GNT::TANGENT, TextureNodeKind::NONE, "The surface tangent at the shaded point"},
    {"Bitangent", GRAPH_CATEGORY_INPUT, GNT::BITANGENT, TextureNodeKind::NONE, "The surface bitangent at the shaded point"},
    {"Texture Coordinate", GRAPH_CATEGORY_INPUT, GNT::TEXCOORD, TextureNodeKind::NONE, "The mesh's texture coordinate at the shaded point"},
    {"Texture Sample", GRAPH_CATEGORY_TEXTURE, GNT::TEXTURE_SAMPLE, TextureNodeKind::NONE, "Reads a texture at a coordinate"},
    {"Image", GRAPH_CATEGORY_TEXTURE, GNT::NONE, TextureNodeKind::ASSET, "A texture asset, read at a coordinate"},
    {"White Noise", GRAPH_CATEGORY_TEXTURE, GNT::NONE, TextureNodeKind::WHITE_NOISE, "A random value per texel"},
    {"Perlin Noise", GRAPH_CATEGORY_TEXTURE, GNT::NONE, TextureNodeKind::PERLIN_NOISE, "Smooth gradient noise"},
    {"Simplex Noise", GRAPH_CATEGORY_TEXTURE, GNT::NONE, TextureNodeKind::SIMPLEX_NOISE, "Smooth noise without a grid's direction"},
    {"Ridged Noise", GRAPH_CATEGORY_TEXTURE, GNT::NONE, TextureNodeKind::RIDGED_NOISE, "Noise folded into sharp ridges"},
    {"Add", GRAPH_CATEGORY_MATH, GNT::ADD_FLOAT, TextureNodeKind::NONE, "Adds two values"},
    {"Subtract", GRAPH_CATEGORY_MATH, GNT::SUBTRACT_FLOAT, TextureNodeKind::NONE, "Subtracts the second value from the first"},
    {"Multiply", GRAPH_CATEGORY_MATH, GNT::MULTIPLY_FLOAT, TextureNodeKind::NONE, "Multiplies two values"},
    {"Divide", GRAPH_CATEGORY_MATH, GNT::DIVIDE_FLOAT, TextureNodeKind::NONE, "Divides the first value by the second"},
    {"Absolute", GRAPH_CATEGORY_MATH, GNT::ABS_FLOAT, TextureNodeKind::NONE, "A value without its sign"},
    {"Minimum", GRAPH_CATEGORY_MATH, GNT::MIN_FLOAT, TextureNodeKind::NONE, "The smaller of two values"},
    {"Maximum", GRAPH_CATEGORY_MATH, GNT::MAX_FLOAT, TextureNodeKind::NONE, "The larger of two values"},
    {"Clamp", GRAPH_CATEGORY_MATH, GNT::CLAMP_FLOAT, TextureNodeKind::NONE, "Keeps a value between two bounds"},
    {"Saturate", GRAPH_CATEGORY_MATH, GNT::SATURATE_FLOAT, TextureNodeKind::NONE, "Keeps a value between zero and one"},
    {"Mix", GRAPH_CATEGORY_MATH, GNT::MIX_FLOAT, TextureNodeKind::NONE, "Blends between two values by a factor"},
    {"Step", GRAPH_CATEGORY_MATH, GNT::STEP_FLOAT, TextureNodeKind::NONE, "Zero below an edge, one from it on"},
    {"Smoothstep", GRAPH_CATEGORY_MATH, GNT::SMOOTHSTEP_FLOAT, TextureNodeKind::NONE, "Eases from zero to one between two edges"},
    {"Fract", GRAPH_CATEGORY_MATH, GNT::FRACT_FLOAT, TextureNodeKind::NONE, "The part of a value after the decimal point"},
    {"Power", GRAPH_CATEGORY_MATH, GNT::POWER_FLOAT, TextureNodeKind::NONE, "Raises a value to a power"},
    {"Square Root", GRAPH_CATEGORY_MATH, GNT::SQRT_FLOAT, TextureNodeKind::NONE, "The square root of a value"},
    {"Sine", GRAPH_CATEGORY_MATH, GNT::SIN_FLOAT, TextureNodeKind::NONE, "The sine of an angle in radians"},
    {"Cosine", GRAPH_CATEGORY_MATH, GNT::COS_FLOAT, TextureNodeKind::NONE, "The cosine of an angle in radians"},
    {"Remap", GRAPH_CATEGORY_MATH, GNT::REMAP_FLOAT, TextureNodeKind::NONE, "Maps a value from one range onto another"},
    {"Dot Product", GRAPH_CATEGORY_MATH, GNT::DOT_VEC3, TextureNodeKind::NONE, "How far two vectors point the same way"},
    {"Cross Product", GRAPH_CATEGORY_MATH, GNT::CROSS_VEC3, TextureNodeKind::NONE, "The vector at right angles to two vectors"},
    {"Normalize", GRAPH_CATEGORY_MATH, GNT::NORMALIZE_VEC3, TextureNodeKind::NONE, "Scales a vector to length one"},
    {"Length", GRAPH_CATEGORY_MATH, GNT::LENGTH_VEC3, TextureNodeKind::NONE, "The length of a vector"},
    {"Distance", GRAPH_CATEGORY_MATH, GNT::DISTANCE_VEC3, TextureNodeKind::NONE, "The distance between two points"},
    {"Combine 2", GRAPH_CATEGORY_VECTOR, GNT::COMBINE_VEC2, TextureNodeKind::NONE, "Builds a vector from two numbers"},
    {"Combine 3", GRAPH_CATEGORY_VECTOR, GNT::COMBINE_VEC3, TextureNodeKind::NONE, "Builds a vector from three numbers"},
    {"Combine 4", GRAPH_CATEGORY_VECTOR, GNT::COMBINE_VEC4, TextureNodeKind::NONE, "Builds a vector from four numbers"},
    {"Split 2", GRAPH_CATEGORY_VECTOR, GNT::SPLIT_VEC2, TextureNodeKind::NONE, "Breaks a vector into its two numbers"},
    {"Split 3", GRAPH_CATEGORY_VECTOR, GNT::SPLIT_VEC3, TextureNodeKind::NONE, "Breaks a vector into its three numbers"},
    {"Split 4", GRAPH_CATEGORY_VECTOR, GNT::SPLIT_VEC4, TextureNodeKind::NONE, "Breaks a vector into its four numbers"},
    {"Normal Map", GRAPH_CATEGORY_VECTOR, GNT::NORMAL_MAP, TextureNodeKind::NONE, "Turns a sampled normal map into a normal"},
    {"Luminance", GRAPH_CATEGORY_COLOR, GNT::LUMINANCE, TextureNodeKind::NONE, "The perceived brightness of a color"},
    {"PBR Surface", GRAPH_CATEGORY_OUTPUT, GNT::SURFACE_OUTPUT, TextureNodeKind::NONE, "The surface the material renders"},
};

/**
 * @brief One typed variant of an operation, paired with its dropdown label
 */
struct NodeVariant {
    const char *label;
    Rapture::GraphNodeType type;
};

/**
 * @brief An operation and its typed variants, swapped in place with a node's dropdown
 */
struct NodeVariantGroup {
    const char *groupLabel;
    std::vector<NodeVariant> variants;
};

static const std::vector<NodeVariantGroup> &s_variantGroups()
{
    static const std::vector<NodeVariantGroup> groups = {
        {"Add", {{"Float", GNT::ADD_FLOAT}, {"Integer", GNT::ADD_INT}, {"Vector", GNT::ADD_VEC3}}},
        {"Subtract", {{"Float", GNT::SUBTRACT_FLOAT}, {"Integer", GNT::SUBTRACT_INT}, {"Vector", GNT::SUBTRACT_VEC3}}},
        {"Multiply", {{"Float", GNT::MULTIPLY_FLOAT}, {"Integer", GNT::MULTIPLY_INT}, {"Vector", GNT::MULTIPLY_VEC3}}},
        {"Divide", {{"Float", GNT::DIVIDE_FLOAT}, {"Integer", GNT::DIVIDE_INT}, {"Vector", GNT::DIVIDE_VEC3}}},
        {"Absolute", {{"Float", GNT::ABS_FLOAT}, {"Integer", GNT::ABS_INT}, {"Vector", GNT::ABS_VEC3}}},
        {"Minimum", {{"Float", GNT::MIN_FLOAT}, {"Integer", GNT::MIN_INT}, {"Vector", GNT::MIN_VEC3}}},
        {"Maximum", {{"Float", GNT::MAX_FLOAT}, {"Integer", GNT::MAX_INT}, {"Vector", GNT::MAX_VEC3}}},
        {"Clamp", {{"Float", GNT::CLAMP_FLOAT}, {"Integer", GNT::CLAMP_INT}, {"Vector", GNT::CLAMP_VEC3}}},
        {"Saturate", {{"Float", GNT::SATURATE_FLOAT}, {"Vector", GNT::SATURATE_VEC3}}},
        {"Mix", {{"Float", GNT::MIX_FLOAT}, {"Vector", GNT::MIX_VEC3}}},
        {"Step", {{"Float", GNT::STEP_FLOAT}, {"Vector", GNT::STEP_VEC3}}},
        {"Smoothstep", {{"Float", GNT::SMOOTHSTEP_FLOAT}, {"Vector", GNT::SMOOTHSTEP_VEC3}}},
        {"Fract", {{"Float", GNT::FRACT_FLOAT}, {"Vector", GNT::FRACT_VEC3}}},
        {"Power", {{"Float", GNT::POWER_FLOAT}, {"Vector", GNT::POWER_VEC3}}},
        {"Square Root", {{"Float", GNT::SQRT_FLOAT}, {"Vector", GNT::SQRT_VEC3}}},
        {"Sine", {{"Float", GNT::SIN_FLOAT}, {"Vector", GNT::SIN_VEC3}}},
        {"Cosine", {{"Float", GNT::COS_FLOAT}, {"Vector", GNT::COS_VEC3}}},
    };
    return groups;
}

/**
 * @brief The variant group a node type belongs to
 * @param type The node type
 * @return The owning group, or nullptr if the type has no typed siblings
 */
static const NodeVariantGroup *s_variantGroupFor(Rapture::GraphNodeType type)
{
    for (const auto &group : s_variantGroups()) {
        for (const auto &variant : group.variants) {
            if (variant.type == type) {
                return &group;
            }
        }
    }
    return nullptr;
}

static bool s_isConstantType(Rapture::GraphNodeType type)
{
    switch (type) {
    case GNT::CONSTANT_FLOAT:
    case GNT::CONSTANT_INT:
    case GNT::CONSTANT_VEC2:
    case GNT::CONSTANT_VEC3:
    case GNT::CONSTANT_VEC4:
        return true;
    default:
        return false;
    }
}

static GraphCategory s_categoryForType(Rapture::GraphNodeType type)
{
    if (s_variantGroupFor(type) != nullptr) {
        return GRAPH_CATEGORY_MATH;
    }
    for (const NodeCatalogEntry &entry : NODE_CATALOG) {
        if (entry.textureKind == TextureNodeKind::NONE && entry.type == type) {
            return entry.category;
        }
    }
    switch (type) {
    case GNT::TEXTURE_WHITE_NOISE:
    case GNT::TEXTURE_PERLIN:
    case GNT::TEXTURE_SIMPLEX:
    case GNT::TEXTURE_RIDGED:
        return GRAPH_CATEGORY_TEXTURE;
    case GNT::NORMAL_MAP_RG:
        return GRAPH_CATEGORY_VECTOR;
    default:
        return GRAPH_CATEGORY_INPUT;
    }
}

static std::string s_nodeTitle(Rapture::GraphNodeType type)
{
    if (const NodeVariantGroup *group = s_variantGroupFor(type)) {
        return group->groupLabel;
    }
    for (const NodeCatalogEntry &entry : NODE_CATALOG) {
        if (entry.textureKind == TextureNodeKind::NONE && entry.type == type) {
            return std::string(entry.name);
        }
    }
    switch (type) {
    case GNT::TEXTURE_WHITE_NOISE:
        return "White Noise";
    case GNT::TEXTURE_PERLIN:
        return "Perlin Noise";
    case GNT::TEXTURE_SIMPLEX:
        return "Simplex Noise";
    case GNT::TEXTURE_RIDGED:
        return "Ridged Noise";
    case GNT::NORMAL_MAP_RG:
        return "Normal Map (RG)";
    default:
        return Rapture::Graph_nodeTypeName(type);
    }
}

static bool s_containsIgnoringCase(std::string_view text, std::string_view search)
{
    if (search.empty()) {
        return true;
    }
    auto it = std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

static std::string s_upperCase(std::string_view text)
{
    std::string upper(text);
    for (char &c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return upper;
}

static void s_setGuiStateFlag(Amethyst::UIObject &object, uint16_t flag, bool on)
{
    const uint16_t state = object.getGuiState();
    object.setGuiState(on ? static_cast<uint16_t>(state | flag) : static_cast<uint16_t>(state & ~flag));
}

// which way a wire leaves a pin: outputs to the right, inputs to the left
static Amethyst::vec2 s_wireBendFor(bool isOutput)
{
    return Amethyst::vec2(isOutput ? WIRE_BEND : -WIRE_BEND, 0.0f);
}

static std::unique_ptr<Rapture::ProceduralTexture> s_createNoiseGenerator(TextureNodeKind kind)
{
    switch (kind) {
    case TextureNodeKind::WHITE_NOISE:
        return Rapture::ProceduralTexture::createWhiteNoiseGenerator();
    case TextureNodeKind::PERLIN_NOISE:
        return Rapture::ProceduralTexture::createPerlinNoiseGenerator();
    case TextureNodeKind::SIMPLEX_NOISE:
        return Rapture::ProceduralTexture::createSimplexNoiseGenerator();
    case TextureNodeKind::RIDGED_NOISE:
        return Rapture::ProceduralTexture::createRidgedNoiseGenerator();
    default:
        return nullptr;
    }
}

// Longest distance of each node from the output along the connection graph, output at level 0. Nodes
// further from the output lay out further left, so a node used at several depths sits left of every consumer.
static std::unordered_map<uint32_t, int> s_computeLevels(const Rapture::MaterialGraph &graph)
{
    std::unordered_map<uint32_t, int> level;
    std::vector<uint32_t> stack;
    level[graph.outputNodeId] = 0;
    stack.push_back(graph.outputNodeId);
    while (!stack.empty()) {
        uint32_t current = stack.back();
        stack.pop_back();
        int next = level[current] + 1;
        for (const auto &connection : graph.connections) {
            if (connection.dstNode != current) {
                continue;
            }
            auto it = level.find(connection.srcNode);
            if (it == level.end() || next > it->second) {
                level[connection.srcNode] = next;
                stack.push_back(connection.srcNode);
            }
        }
    }
    return level;
}

NodeEditorPanel::NodeEditorPanel(const WorkspaceContext &context, Rapture::AssetHandle material)
    : Panel("Surface Graph", context), m_material(material)
{
    m_icon = Icons::SVG_MATERIAL;
    Rapture::GraphDomainRegistry::registerBuiltins();
}

NodeEditorPanel::~NodeEditorPanel() = default;

void NodeEditorPanel::onRootDestroyed()
{
    m_canvas = nullptr;
    m_canvasDots = nullptr;
    m_content = nullptr;
    m_wireLayer = nullptr;
    m_nodeMenu = nullptr;
    m_addNodeMenu = nullptr;
    m_addNodeSearch = nullptr;
    m_addNodeResults = nullptr;
    m_dragWire = nullptr;
    m_connecting = false;
    m_nodes.clear();
    m_pins = {};
    m_connections.clear();
    m_selectedNodes.clear();
    m_primaryNodeId = 0;
}

void NodeEditorPanel::build(Amethyst::UIScope &scope)
{
    (void)scope;

    setupCanvas();
    setupCanvasOverlays();
    setupAddNodeMenu();
    setupNodeMenu();

    m_context.editor->shortcuts->registerShortcut(EDITOR_COMMAND_DELETE_SELECTION, {Rapture::KEY_DELETE}, m_root, [this]() {
        if (!m_selectedNodes.empty()) {
            deleteSelection();
        }
    });

    loadMaterial(m_material);
}

void NodeEditorPanel::setupCanvas()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"graph-canvas"},
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2::fromOffset(CANVAS_INSET, CANVAS_INSET),
                    .size = Amethyst::UDim2(1.0f, -2.0f * CANVAS_INSET, 1.0f, -2.0f * CANVAS_INSET),
                },
        },
        [this](Amethyst::FrameScope &canvas) { m_canvas = &canvas.component; });

    Amethyst::UIScope(*m_canvas).frame(
        {
            .classes = {"graph-canvas-dots"},
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &dots) { m_canvasDots = &dots.component; });
    m_canvasDots->ignore(Amethyst::INTERACTION_CATEGORY_ALL);

    Amethyst::UIScope(*m_canvas).frame(
        {
            .base = {.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f), .zIndex = 1},
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &content) { m_content = &content.component; });
    m_content->ignore(Amethyst::INTERACTION_CATEGORY_ALL);

    // added first so the wires sit under every node
    Amethyst::UIScope(*m_content).frame(
        {
            .base = {.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &wires) { m_wireLayer = &wires.component; });
    m_wireLayer->ignore(Amethyst::INTERACTION_CATEGORY_ALL);

    setPan(INITIAL_PAN);

    m_canvasBeganConn = m_canvas->onInputBeganCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputBegan(io); });
    m_canvasMovedConn = m_canvas->onInputChangedCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputChanged(io); });
    m_canvasEndedConn = m_canvas->onInputEndedCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputEnded(io); });
}

void NodeEditorPanel::setupCanvasOverlays()
{
    Amethyst::Frame *breadcrumb = nullptr;
    Amethyst::UIScope(*m_canvas).frame(
        {
            .classes = {"graph-toolbar"},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(OVERLAY_PADDING),
                                               .right = Amethyst::UDim::fromOffset(CRUMB_PADDING),
                                               .bottom = Amethyst::UDim::fromOffset(OVERLAY_PADDING),
                                               .left = Amethyst::UDim::fromOffset(CRUMB_PADDING)},
                    .position = Amethyst::UDim2::fromOffset(OVERLAY_MARGIN, OVERLAY_MARGIN),
                    .size = Amethyst::UDim2::fromOffset(0.0f, OVERLAY_HEIGHT),
                    .zIndex = OVERLAY_Z_INDEX,
                },
        },
        [&breadcrumb](Amethyst::FrameScope &toolbar) { breadcrumb = &toolbar.component; });
    auto *crumbs = breadcrumb->addExtension<Amethyst::UIListLayout>();
    crumbs->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    crumbs->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    crumbs->innerPadding = Amethyst::UDim::fromOffset(CRUMB_PADDING);

    const std::string materialName = Rapture::AssetManager::getAssetMetadata(m_material).getName();
    Amethyst::UIScope crumbScope(*breadcrumb);
    crumbScope.imageLabel({
        .classes = {"graph-crumb-icon"},
        .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(CRUMB_ICON_SIZE, CRUMB_ICON_SIZE)},
        .image = {.imageColor = Amethyst::Color4(Asset_colorForType(Rapture::ASSET_MATERIAL), 1.0f)},
        .svg = Icons::SVG_MATERIAL,
    });
    crumbScope.textLabel({
        .classes = {"graph-crumb"},
        .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
        .label = materialName,
    });
    crumbScope.imageLabel({
        .classes = {"graph-crumb-separator"},
        .base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromOffset(CRUMB_SEPARATOR_SIZE, CRUMB_SEPARATOR_SIZE)},
        .svg = Icons::SVG_CARET_RIGHT,
    });
    crumbScope.textLabel({
        .classes = {"graph-crumb-current"},
        .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = 3, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
        .label = std::string(SURFACE_CRUMB),
    });
    for (const auto &child : breadcrumb->getChildren()) {
        if (auto *object = child->asUiObject()) {
            object->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        }
    }

    Amethyst::Frame *viewTools = nullptr;
    Amethyst::UIScope(*m_canvas).frame(
        {
            .classes = {"graph-toolbar"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(1.0f, 0.0f),
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .padding = Amethyst::UDim4::fromOffset(OVERLAY_PADDING),
                    .position = Amethyst::UDim2(1.0f, -OVERLAY_MARGIN, 0.0f, OVERLAY_MARGIN),
                    .size = Amethyst::UDim2::fromOffset(0.0f, OVERLAY_HEIGHT),
                    .zIndex = OVERLAY_Z_INDEX,
                },
        },
        [&viewTools](Amethyst::FrameScope &toolbar) { viewTools = &toolbar.component; });
    auto *tools = viewTools->addExtension<Amethyst::UIListLayout>();
    tools->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    tools->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    tools->innerPadding = Amethyst::UDim::fromOffset(OVERLAY_GAP);

    auto *frameAll = viewTools->add<IconButton>(Icons::SVG_PERSPECTIVE, OVERLAY_ICON_SIZE, std::vector<std::string>{"graph-toolbar-button"});
    frameAll->setBaseProperties({.size = Amethyst::UDim2::fromOffset(OVERLAY_BUTTON_SIZE, OVERLAY_BUTTON_SIZE)});
    frameAll->track(frameAll->onMouseButton1ClickCb.connect([this]() { frameAllNodes(); }));
    ControlTooltip_attach(*frameAll, FRAME_ALL_TOOLTIP, *m_context.editor);
}

void NodeEditorPanel::setupAddNodeMenu()
{
    Amethyst::UIScope(*m_root).popup(
        {
            .classes = {"graph-add-menu"},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::Y,
                    .padding = Amethyst::UDim4::fromOffset(ADD_MENU_PADDING),
                    .size = Amethyst::UDim2::fromOffset(ADD_MENU_WIDTH, 0.0f),
                },
        },
        [this](Amethyst::PopupScope &p) { m_addNodeMenu = &p.component; });
    auto *stack = m_addNodeMenu->addExtension<Amethyst::UIListLayout>();
    stack->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
    stack->innerPadding = Amethyst::UDim::fromOffset(ADD_MENU_GAP);

    m_addNodeSearch = m_addNodeMenu->add<SearchField>("Search nodes");
    m_addNodeSearch->setBaseProperties({.layoutOrder = 0, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ADD_MENU_SEARCH_HEIGHT)});
    m_addNodeSearch->setOnTextChanged([this](const std::string &text) { fillAddNodeResults(text); });
    m_addNodeSearch->setOnEnterPressed([this]() {
        if (!m_addNodeMatches.empty()) {
            addNodeFromCatalog(m_addNodeMatches.front());
        }
    });

    Amethyst::UIScope(*m_addNodeMenu)
        .scrollingFrame(
            {
                .classes = {"graph-add-results"},
                .base = {.layoutOrder = 1, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ADD_MENU_LIST_HEIGHT)},
                .scroll =
                    {
                        .scrollAxis = Amethyst::ScrollAxis::Y,
                        .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                        .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                    },
            },
            [this](Amethyst::ScrollingFrameScope &sf) { m_addNodeResults = &sf.component; });
    auto *rows = m_addNodeResults->addExtension<Amethyst::UIListLayout>();
    rows->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
}

void NodeEditorPanel::setupNodeMenu()
{
    m_nodeMenu = m_root->add<Amethyst::ContextMenu>();
}

void NodeEditorPanel::loadMaterial(Rapture::AssetHandle handle)
{
    Rapture::AssetRef ref = Rapture::AssetManager::getAsset(handle);
    if (!ref) {
        RP_ERROR("Could not open material {}", static_cast<uint64_t>(handle));
        return;
    }
    Rapture::AMaterialInstance *instanceAsset = ref->as<Rapture::AMaterialInstance>();
    if (instanceAsset == nullptr) {
        RP_ERROR("Asset {} holds no material instance", static_cast<uint64_t>(handle));
        return;
    }
    Rapture::BaseMaterial *base = instanceAsset->material().getBaseMaterial();
    if (base == nullptr) {
        RP_ERROR("Material '{}' has no base material", Rapture::AssetManager::getAssetMetadata(handle).getName());
        return;
    }

    m_selectedGraphId = base->getGraphId();
    loadGraph(base->getGraph());

    m_onMaterialSelectionChanged.fire(handle);
}

void NodeEditorPanel::loadGraph(const Rapture::MaterialGraph &graph)
{
    clearGraph();
    if (graph.nodes.empty()) {
        return;
    }

    static constexpr float COLUMN_SPACING = NODE_WIDTH + 90.0f;
    static constexpr float ROW_SPACING = 260.0f;

    std::unordered_map<uint32_t, int> level = s_computeLevels(graph);
    int maxLevel = 0;
    for (const auto &[id, lvl] : level) {
        maxLevel = std::max(maxLevel, lvl);
    }

    std::unordered_map<uint32_t, uint32_t> idMap;
    std::unordered_map<int, int> columnRows;
    for (const auto &node : graph.nodes) {
        auto lvlIt = level.find(node.id);
        int lvl = (lvlIt != level.end()) ? lvlIt->second : maxLevel + 1;
        int column = maxLevel - lvl;
        int row = columnRows[lvl]++;
        Amethyst::vec2 pos{static_cast<float>(column) * COLUMN_SPACING, static_cast<float>(row) * ROW_SPACING};

        uint32_t spawnedId = spawnNode(node.type, pos);
        idMap[node.id] = spawnedId;

        if (node.type == GNT::TEXTURE_SAMPLE && !node.inputTextures.empty() && node.inputTextures[0].get() != nullptr) {
            setImageTexture(spawnedId, node.inputTextures[0]);
        }

        for (uint32_t i = 0; i < node.inputValues.size(); ++i) {
            if (!node.inputValues[i].has_value()) {
                continue;
            }
            uint32_t pinId = findPin(spawnedId, false, i);
            if (pinId != INVALID_PIN && m_pins[pinId].value != nullptr) {
                *m_pins[pinId].value = *node.inputValues[i];
            }
        }
    }

    for (const auto &connection : graph.connections) {
        auto srcIt = idMap.find(connection.srcNode);
        auto dstIt = idMap.find(connection.dstNode);
        if (srcIt == idMap.end() || dstIt == idMap.end()) {
            continue;
        }
        connectPins(srcIt->second, connection.srcPin, dstIt->second, connection.dstPin);
    }
}

void NodeEditorPanel::onCanvasInputBegan(const Amethyst::InputObject &io)
{
    const Amethyst::vec2 mouse(io.position.x, io.position.y);

    if (io.type == Amethyst::InputType::MOUSE_BUTTON_2) {
        openAddNodeMenu(mouse);
        return;
    }

    if (io.type == Amethyst::InputType::MOUSE_BUTTON_3) {
        m_panning = true;
        m_panLastMouse = mouse;
        if (auto *window = m_canvas->getWindow()) {
            window->captureMouse(m_canvas);
        }
        return;
    }

    if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
        clearSelection();
    }
}

void NodeEditorPanel::onCanvasInputChanged(const Amethyst::InputObject &io)
{
    const Amethyst::vec2 mouse(io.position.x, io.position.y);

    if (m_panning) {
        setPan(m_pan + (mouse - m_panLastMouse));
        m_panLastMouse = mouse;
        return;
    }

    if (m_connecting) {
        updateConnectionDrag(mouse);
    }
}

void NodeEditorPanel::onCanvasInputEnded(const Amethyst::InputObject &io)
{
    if (m_panning && io.type == Amethyst::InputType::MOUSE_BUTTON_3) {
        m_panning = false;
        if (auto *window = m_canvas->getWindow()) {
            window->releaseMouse(m_canvas);
        }
        return;
    }

    if (m_connecting && io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
        finishConnection(Amethyst::vec2(io.position.x, io.position.y));
    }
}

void NodeEditorPanel::setPan(Amethyst::vec2 pan)
{
    m_pan = pan;
    m_content->setBaseProperties({.position = Amethyst::UDim2::fromOffset(pan.x, pan.y)});
    m_canvasDots->setBaseStyleProperties({.backgroundPosition = pan});
}

void NodeEditorPanel::frameAllNodes()
{
    if (m_nodes.empty()) {
        return;
    }

    Amethyst::vec2 lowest(std::numeric_limits<float>::max());
    Amethyst::vec2 highest(std::numeric_limits<float>::lowest());
    for (const auto &[nodeId, view] : m_nodes) {
        const Amethyst::vec2 topLeft = view.frame->getBaseProperties().position.offset;
        lowest = Amethyst::min(lowest, topLeft);
        highest = Amethyst::max(highest, topLeft + view.frame->absoluteSize);
    }

    setPan(m_canvas->absoluteSize * 0.5f - (lowest + highest) * 0.5f);
}

void NodeEditorPanel::openAddNodeMenu(Amethyst::vec2 screenPos)
{
    m_addNodeScreenPos = screenPos;
    m_addNodeSearch->clear();
    fillAddNodeResults({});
    m_addNodeMenu->openAt(screenPos + ADD_MENU_CURSOR_OFFSET);
    m_addNodeSearch->focus();
}

void NodeEditorPanel::fillAddNodeResults(std::string_view search)
{
    std::vector<std::unique_ptr<Amethyst::Instance>> previous;
    while (!m_addNodeResults->getChildren().empty()) {
        previous.push_back(m_addNodeResults->removeChild(m_addNodeResults->getChildren().back().get()));
    }
    m_addNodeMatches.clear();

    uint32_t order = 0;
    for (int category = 0; category < GRAPH_CATEGORY_COUNT; ++category) {
        const GraphCategory graphCategory = static_cast<GraphCategory>(category);
        bool sectionAdded = false;

        for (size_t index = 0; index < std::size(NODE_CATALOG); ++index) {
            const NodeCatalogEntry &entry = NODE_CATALOG[index];
            if (entry.category != graphCategory || !s_containsIgnoringCase(entry.name, search)) {
                continue;
            }

            Amethyst::UIScope results(*m_addNodeResults);
            if (!sectionAdded) {
                sectionAdded = true;
                results.frame(
                    {
                        .classes = {"graph-add-section"},
                        .base =
                            {
                                .layoutOrder = order++,
                                .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(ADD_SECTION_PADDING),
                                                           .left = Amethyst::UDim::fromOffset(ADD_SECTION_PADDING)},
                                .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ADD_SECTION_HEIGHT),
                            },
                    },
                    [graphCategory](Amethyst::FrameScope &section) {
                        section.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL);
                        auto *row = section.component.addExtension<Amethyst::UIListLayout>();
                        row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                        row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
                        row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                        row->innerPadding = Amethyst::UDim::fromOffset(ADD_ENTRY_GAP);
                        section.textLabel(
                            {
                                .classes = {"graph-add-section-title"},
                                .base = {.automaticSize = Amethyst::AutomaticSize::X,
                                         .layoutOrder = 0,
                                         .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                                .label = s_upperCase(GraphCategory_name(graphCategory)),
                            },
                            [](Amethyst::TextLabelScope &title) { title.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });
                        section.frame({.classes = {"graph-add-section-line"}, .base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(0.0f, 1.0f)}},
                                      [](Amethyst::FrameScope &line) {
                                          line.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL);
                                          line.component.addExtension<Amethyst::UIFlexItem>();
                                      });
                    });
            }

            const bool highlighted = m_addNodeMatches.empty();
            m_addNodeMatches.push_back(index);
            results.textButton(
                {
                    .classes = {"graph-add-entry"},
                    .base =
                        {
                            .layoutOrder = order++,
                            .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(ADD_ENTRY_PADDING),
                                                       .left = Amethyst::UDim::fromOffset(ADD_ENTRY_PADDING)},
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ADD_ENTRY_HEIGHT),
                        },
                },
                [this, &entry, index, highlighted](Amethyst::TextButtonScope &button) {
                    s_setGuiStateFlag(button.component, Amethyst::GUI_STATE_ACTIVE, highlighted);
                    auto *row = button.component.addExtension<Amethyst::UIListLayout>();
                    row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
                    row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    row->innerPadding = Amethyst::UDim::fromOffset(ADD_ENTRY_GAP);

                    button.frame({
                        .classes = {"graph-add-swatch"},
                        .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(ADD_SWATCH_SIZE, ADD_SWATCH_SIZE)},
                        .style = {.backgroundColor = GraphCategory_color(entry.category)},
                    });
                    button.textLabel({
                        .classes = {"graph-add-name"},
                        .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                        .label = std::string(entry.name),
                    });
                    button.textLabel(
                        {
                            .classes = {"graph-add-description"},
                            .base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                            .text = {.textTruncate = Amethyst::TextTruncate::ELLIPSIS},
                            .label = std::string(entry.description),
                        },
                        [](Amethyst::TextLabelScope &description) { description.component.addExtension<Amethyst::UIFlexItem>(); });
                    for (const auto &child : button.component.getChildren()) {
                        if (auto *object = child->asUiObject()) {
                            object->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
                        }
                    }

                    button.component.track(button.component.onMouseButton1ClickCb.connect([this, index]() { addNodeFromCatalog(index); }));
                });
        }
    }
}

void NodeEditorPanel::addNodeFromCatalog(size_t catalogIndex)
{
    const NodeCatalogEntry &entry = NODE_CATALOG[catalogIndex];
    const Amethyst::vec2 canvasPos = m_addNodeScreenPos - m_content->absolutePosition;

    const uint32_t nodeId = entry.textureKind == TextureNodeKind::NONE ? spawnNode(entry.type, canvasPos)
                                                                       : spawnTextureNode(entry.textureKind, entry.name, canvasPos);
    m_addNodeMenu->close();
    selectNode(nodeId, false);
    m_onNodeEdited.fire(nodeId);
}

uint32_t NodeEditorPanel::createNodeShell(Amethyst::vec2 canvasPos, std::string_view title, GraphCategory category)
{
    const uint32_t nodeId = m_nextNodeId++;
    NodeView &view = m_nodes[nodeId];
    view.title = std::string(title);
    view.category = category;

    // pin sockets straddle the node's edges, so it never clips what it holds
    Amethyst::UIScope(*m_content).frame(
        {
            .classes = {"graph-node"},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::Y,
                    .clipsDescendants = false,
                    .position = Amethyst::UDim2::fromOffset(canvasPos.x, canvasPos.y),
                    .size = Amethyst::UDim2::fromOffset(NODE_WIDTH, 0.0f),
                    .zIndex = 1,
                },
        },
        [&view](Amethyst::FrameScope &node) { view.frame = &node.component; });
    auto *stack = view.frame->addExtension<Amethyst::UIListLayout>();
    stack->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

    auto *drag = view.frame->addExtension<Amethyst::UIDragDetector>();
    drag->mode = Amethyst::DragMode::FREE;

    view.frame->track(view.frame->onInputBeganCb.connect([this, nodeId](const Amethyst::InputObject &io) {
        if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
            selectNode(nodeId, false);
        } else if (io.type == Amethyst::InputType::MOUSE_BUTTON_2) {
            showNodeMenu(nodeId, Amethyst::vec2(io.position.x, io.position.y));
        }
    }));

    Amethyst::UIScope(*view.frame)
        .frame(
            {
                .classes = {"graph-node-header"},
                .base =
                    {
                        .clipsDescendants = true,
                        .layoutOrder = 0,
                        .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(NODE_HEADER_PADDING),
                                                   .left = Amethyst::UDim::fromOffset(NODE_HEADER_PADDING)},
                        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, NODE_HEADER_HEIGHT),
                    },
                .style = {.backgroundColor = GraphCategory_headerFill(category)},
            },
            [&](Amethyst::FrameScope &header) {
                header.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL);
                auto *row = header.component.addExtension<Amethyst::UIListLayout>();
                row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                row->innerPadding = Amethyst::UDim::fromOffset(NODE_HEADER_GAP);

                header.imageLabel(
                    {
                        .classes = {"graph-node-icon"},
                        .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(NODE_ICON_SIZE, NODE_ICON_SIZE)},
                        .svg = GraphCategory_icon(category),
                    },
                    [](Amethyst::ImageLabelScope &icon) { icon.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });
                header.textLabel(
                    {
                        .classes = {"graph-node-title"},
                        .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                        .label = std::string(title),
                    },
                    [](Amethyst::TextLabelScope &label) { label.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });
            });

    Amethyst::UIScope(*view.frame)
        .frame(
            {
                .base =
                    {
                        .automaticSize = Amethyst::AutomaticSize::Y,
                        .clipsDescendants = false,
                        .layoutOrder = 1,
                        .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(NODE_BODY_PADDING_TOP),
                                                   .bottom = Amethyst::UDim::fromOffset(NODE_BODY_PADDING_BOTTOM)},
                        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 0.0f),
                    },
                .style = {.backgroundTransparency = 1.0f},
            },
            [&view](Amethyst::FrameScope &body) { view.body = &body.component; });
    view.body->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
    auto *rows = view.body->addExtension<Amethyst::UIListLayout>();
    rows->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
    rows->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_CENTER_H;

    return nodeId;
}

uint32_t NodeEditorPanel::spawnNode(Rapture::GraphNodeType type, Amethyst::vec2 canvasPos)
{
    const uint32_t nodeId = createNodeShell(canvasPos, s_nodeTitle(type), s_categoryForType(type));
    m_nodes[nodeId].type = type;
    if (s_variantGroupFor(type) != nullptr) {
        addVariantRow(nodeId);
    }
    buildNodeRows(nodeId);
    return nodeId;
}

uint32_t NodeEditorPanel::spawnTextureNode(TextureNodeKind kind, std::string_view title, Amethyst::vec2 canvasPos)
{
    const uint32_t nodeId = createNodeShell(canvasPos, title, GRAPH_CATEGORY_TEXTURE);

    NodeView &view = m_nodes[nodeId];
    view.textureData = std::make_unique<TextureNodeData>();
    view.textureData->kind = kind;
    if (kind != TextureNodeKind::ASSET) {
        view.textureData->generator = s_createNoiseGenerator(kind);
    }

    buildTextureNodeRows(nodeId);
    if (kind != TextureNodeKind::ASSET) {
        regenerateProcedural(nodeId);
    }
    return nodeId;
}

Amethyst::Frame *NodeEditorPanel::addNodeRow(uint32_t nodeId, float height)
{
    NodeView &view = m_nodes[nodeId];
    Amethyst::Frame *row = nullptr;
    Amethyst::UIScope(*view.body).frame(
        {
            .base =
                {
                    .clipsDescendants = false,
                    .layoutOrder = view.nextRowOrder++,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, height),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [&row](Amethyst::FrameScope &f) { row = &f.component; });
    row->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
    view.rows.push_back(row);
    return row;
}

void NodeEditorPanel::buildNodeRows(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];
    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(view.type);
    if (def == nullptr) {
        return;
    }

    uint32_t slot = 0;
    for (const auto &pin : def->outputs) {
        addPinRow(nodeId, pin.name, pin.type, slot++, true, {});
    }

    slot = 0;
    if (s_isConstantType(view.type)) {
        // a constant edits its value through its own fields, so its input has no socket
        for (const auto &pin : def->inputs) {
            PinView value;
            value.nodeId = nodeId;
            value.slotIndex = slot++;
            value.type = pin.type;
            value.name = pin.name;
            value.value = std::make_unique<Rapture::PinValue>(pin.defaultValue);
            view.pinIds.push_back(m_pins.insert(std::move(value)));
        }
        addConstantRows(nodeId);
    } else {
        for (const auto &pin : def->inputs) {
            addPinRow(nodeId, pin.name, pin.type, slot++, false, pin.defaultValue);
        }
    }

    if (view.type == GNT::TEXTURE_SAMPLE) {
        if (view.textureData == nullptr) {
            view.textureData = std::make_unique<TextureNodeData>();
        }
        view.textureData->preview = addTexturePreview(nodeId);
        if (view.textureData->texture) {
            setImageTexture(nodeId, view.textureData->texture);
        }
    }

    for (uint32_t pinId : view.pinIds) {
        applyPinState(pinId);
    }
}

void NodeEditorPanel::buildTextureNodeRows(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];

    addPinRow(nodeId, "Color", Rapture::PinType::VEC3, 0, true, {});
    addPinRow(nodeId, "Alpha", Rapture::PinType::FLOAT, 1, true, {});
    // an unconnected UV falls back to the mesh's texture coordinates when the node is lowered for compiling
    addPinRow(nodeId, "UV", Rapture::PinType::VEC2, 0, false, {});

    view.textureData->preview = addTexturePreview(nodeId);

    if (view.textureData->generator != nullptr && view.textureData->generator->isValid()) {
        const auto &descs = view.textureData->generator->getParameters();
        for (size_t index = 0; index < descs.size(); ++index) {
            const Rapture::ProceduralParameter &desc = descs[index];
            if (desc.hidden) {
                continue;
            }

            auto param = std::make_unique<TextureParam>();
            param->index = index;
            param->f = view.textureData->generator->getParameterFloat(index);
            param->i = view.textureData->generator->getParameterInt(index);
            addTextureParamRow(nodeId, desc, param.get());
            view.textureData->params.push_back(std::move(param));
        }
    }

    for (uint32_t pinId : view.pinIds) {
        applyPinState(pinId);
    }
}

void NodeEditorPanel::addVariantRow(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];
    const NodeVariantGroup *group = s_variantGroupFor(view.type);

    Amethyst::UIScope(*view.body).frame(
        {
            .base = {.layoutOrder = view.nextRowOrder++, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, NODE_VARIANT_ROW_HEIGHT)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&view](Amethyst::FrameScope &row) { view.variantRow = &row.component; });
    view.variantRow->ignore(Amethyst::INTERACTION_CATEGORY_ALL);

    Amethyst::UIScope(*view.variantRow)
        .dropdown(
            {
                .classes = {"property-input-field", "property-dropdown"},
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                        .position = Amethyst::UDim2(0.0f, NODE_FIELD_INSET, 0.5f, 0.0f),
                        .size = Amethyst::UDim2(1.0f, -2.0f * NODE_FIELD_INSET, 0.0f, NODE_VARIANT_ROW_HEIGHT - 6.0f),
                        .zIndex = 2,
                    },
            },
            [&view](Amethyst::DropdownScope &d) { view.variantDropdown = &d.component; });

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    for (size_t index = 0; index < group->variants.size(); ++index) {
        items.push_back(Amethyst::makeActionItem(group->variants[index].label, [this, nodeId, index]() { setNodeVariant(nodeId, index); }));
    }
    view.variantDropdown->setItems(std::move(items));
    view.variantDropdown->setText(std::string(getNodeVariantLabel(nodeId)));
}

void NodeEditorPanel::addConstantRows(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];
    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(view.type);
    const uint32_t valuePinId = findPin(nodeId, false, 0);
    if (def == nullptr || def->outputs.empty() || valuePinId == INVALID_PIN) {
        return;
    }

    Rapture::PinValue *value = m_pins[valuePinId].value.get();
    const Rapture::PinType type = def->outputs[0].type;
    const bool wholeNumbers = type == Rapture::PinType::INT;
    const uint32_t components = Rapture::graph_pinTypeComponents(type);
    const float fieldLeft = NODE_ROW_INSET + NODE_AXIS_LABEL_WIDTH + NODE_AXIS_FIELD_GAP;

    for (uint32_t c = 0; c < components; ++c) {
        Amethyst::Frame *row = addNodeRow(nodeId, NODE_ROW_HEIGHT);
        Amethyst::UIScope rowScope(*row);
        rowScope.textLabel(
            {
                .classes = {"graph-node-axis-label"},
                .base =
                    {
                        .position = Amethyst::UDim2::fromOffset(NODE_ROW_INSET, 0.0f),
                        .size = Amethyst::UDim2(0.0f, NODE_AXIS_LABEL_WIDTH, 1.0f, 0.0f),
                    },
                .label = AXIS_NAMES[c],
            },
            [](Amethyst::TextLabelScope &label) { label.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });

        rowScope.dragFloat(
            {
                .classes = {"property-input-field", "graph-node-field"},
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                        .position = Amethyst::UDim2(0.0f, fieldLeft, 0.5f, 0.0f),
                        .size = Amethyst::UDim2(1.0f, -(fieldLeft + NODE_FIELD_INSET), 0.0f, NODE_FIELD_HEIGHT),
                        .zIndex = 2,
                    },
                .format = wholeNumbers ? "%.0f" : "%.3f",
                .speed = wholeNumbers ? 1.0 : 0.01,
            },
            [this, &view, nodeId, value, c](Amethyst::DragFloatScope &d) {
                d.component.valueF = &value->v4[c];
                d.component.onValueChanged = [this, nodeId](double) { m_onNodeValuesChanged.fire(nodeId); };
                if (c < AXIS_BAR_CLASSES.size()) {
                    d.frame(
                        {
                            .classes = {std::string(AXIS_BAR_CLASSES[c])},
                            .base = {.size = Amethyst::UDim2(0.0f, NODE_AXIS_BAR_WIDTH, 1.0f, 0.0f)},
                        },
                        [](Amethyst::FrameScope &bar) { bar.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });
                }
                view.valueFields.push_back(&d.component);
            });
    }
}

uint32_t NodeEditorPanel::addPinRow(uint32_t nodeId, std::string_view name, Rapture::PinType type, uint32_t slotIndex, bool isOutput,
                                    const Rapture::PinValue &defaultValue)
{
    Amethyst::Frame *row = addNodeRow(nodeId, NODE_ROW_HEIGHT);
    const float edge = isOutput ? 1.0f : 0.0f;

    auto *socket = row->add<Amethyst::Shape>(Amethyst::ShapeKind::CIRCLE);
    socket->addClass("graph-pin");
    socket->setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
        .position = Amethyst::UDim2(edge, 0.0f, 0.5f, 0.0f),
        .size = Amethyst::UDim2::fromOffset(PIN_SOCKET_SIZE, PIN_SOCKET_SIZE),
        .zIndex = 2,
    });

    Amethyst::UIScope(*row).textLabel(
        {
            .classes = {"graph-node-pin-label"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(edge, 0.0f),
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .position = Amethyst::UDim2(edge, isOutput ? -NODE_ROW_INSET : NODE_ROW_INSET, 0.0f, 0.0f),
                    .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
                },
            .label = std::string(name),
        },
        [](Amethyst::TextLabelScope &label) { label.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });

    PinView pin;
    pin.socket = socket;
    pin.nodeId = nodeId;
    pin.slotIndex = slotIndex;
    pin.isOutput = isOutput;
    pin.type = type;
    pin.name = std::string(name);
    if (!isOutput) {
        pin.value = std::make_unique<Rapture::PinValue>(defaultValue);
    }

    if (!isOutput && type != Rapture::PinType::TEXTURE && Rapture::graph_pinTypeComponents(type) == 1) {
        const bool wholeNumbers = type == Rapture::PinType::INT;
        auto *field = row->add<Amethyst::DragFloat>();
        field->setClasses({"property-input-field", "graph-node-field"});
        field->valueF = &pin.value->v4[0];
        field->speed = wholeNumbers ? 1.0 : 0.01;
        field->setFormat(wholeNumbers ? "%.0f" : "%.3f");
        field->setBaseProperties({
            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
            .position = Amethyst::UDim2(1.0f, -NODE_FIELD_INSET, 0.5f, 0.0f),
            .size = Amethyst::UDim2::fromOffset(NODE_FIELD_WIDTH, NODE_FIELD_HEIGHT),
            .zIndex = 2,
        });
        field->onValueChanged = [this, nodeId](double) { m_onNodeValuesChanged.fire(nodeId); };
        pin.inlineField = field;
        m_nodes[nodeId].valueFields.push_back(field);
    }

    const uint32_t pinId = m_pins.insert(std::move(pin));
    m_nodes[nodeId].pinIds.push_back(pinId);
    socket->track(socket->onInputBeganCb.connect([this, pinId](const Amethyst::InputObject &io) {
        if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
            beginConnection(pinId, Amethyst::vec2(io.position.x, io.position.y));
        }
    }));
    return pinId;
}

Amethyst::ImageLabel *NodeEditorPanel::addTexturePreview(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];
    const float side = NODE_WIDTH - 2.0f * NODE_PREVIEW_INSET;

    Amethyst::ImageLabel *preview = nullptr;
    Amethyst::UIScope(*view.body).imageLabel(
        {
            .classes = {"graph-node-preview"},
            .base = {.layoutOrder = view.nextRowOrder++, .size = Amethyst::UDim2::fromOffset(side, side)},
        },
        [&preview](Amethyst::ImageLabelScope &il) { preview = &il.component; });
    preview->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
    view.rows.push_back(preview);
    return preview;
}

void NodeEditorPanel::addTextureParamRow(uint32_t nodeId, const Rapture::ProceduralParameter &desc, TextureParam *param)
{
    Amethyst::Frame *row = addNodeRow(nodeId, NODE_ROW_HEIGHT);
    Amethyst::UIScope(*row).textLabel(
        {
            .classes = {"graph-node-pin-label"},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .position = Amethyst::UDim2::fromOffset(NODE_ROW_INSET, 0.0f),
                    .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
                },
            .label = desc.displayName,
        },
        [](Amethyst::TextLabelScope &label) { label.component.ignore(Amethyst::INTERACTION_CATEGORY_ALL); });

    const Amethyst::BasePropertiesArgs fieldPlacement = {
        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
        .position = Amethyst::UDim2(1.0f, -NODE_FIELD_INSET, 0.5f, 0.0f),
        .size = Amethyst::UDim2::fromOffset(NODE_FIELD_WIDTH, NODE_FIELD_HEIGHT),
        .zIndex = 2,
    };
    const bool isInteger =
        desc.type == Rapture::PushConstantMemberInfo::BaseType::INT || desc.type == Rapture::PushConstantMemberInfo::BaseType::UINT;

    if (isInteger) {
        auto *field = row->add<Amethyst::DragInt>();
        field->setClasses({"property-input-field", "graph-node-field"});
        field->value = &param->i;
        field->speed = 1;
        if (desc.hasRange) {
            field->min = static_cast<int64_t>(desc.minValue);
            field->max = static_cast<int64_t>(desc.maxValue);
        }
        field->onValueChanged = [this, nodeId](int64_t) {
            regenerateProcedural(nodeId);
            m_onNodeValuesChanged.fire(nodeId);
        };
        field->setBaseProperties(fieldPlacement);
        return;
    }

    auto *field = row->add<Amethyst::DragFloat>();
    field->setClasses({"property-input-field", "graph-node-field"});
    field->value = &param->f;
    field->speed = desc.hasRange ? (desc.maxValue - desc.minValue) * 0.01 : 0.01;
    field->setFormat("%.3f");
    if (desc.hasRange) {
        field->min = desc.minValue;
        field->max = desc.maxValue;
    }
    field->onValueChanged = [this, nodeId](double) {
        regenerateProcedural(nodeId);
        m_onNodeValuesChanged.fire(nodeId);
    };
    field->setBaseProperties(fieldPlacement);
}

void NodeEditorPanel::regenerateProcedural(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.textureData == nullptr) {
        return;
    }
    TextureNodeData &td = *it->second.textureData;
    if (td.generator == nullptr || !td.generator->isValid()) {
        return;
    }

    // push the edited field values back into the generator, then run its compute pass again
    const auto &descs = td.generator->getParameters();
    for (const auto &param : td.params) {
        if (param->index >= descs.size()) {
            continue;
        }
        Rapture::PushConstantMemberInfo::BaseType type = descs[param->index].type;
        if (type == Rapture::PushConstantMemberInfo::BaseType::INT || type == Rapture::PushConstantMemberInfo::BaseType::UINT) {
            td.generator->setParameterInt(param->index, param->i);
        } else {
            td.generator->setParameterFloat(param->index, param->f);
        }
    }

    td.generator->generate();
    td.texture = td.generator->getTextureAsset();

    if (td.preview != nullptr) {
        td.preview->setImage(m_context.editor->textures->registerTexture(&td.generator->getTexture()));
    }
}

void NodeEditorPanel::setImageTexture(uint32_t nodeId, Rapture::Ref<Rapture::ATexture> texture)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.textureData == nullptr) {
        return;
    }
    Rapture::Texture *gpuTexture = texture.operator->();
    it->second.textureData->texture = std::move(texture);

    if (gpuTexture != nullptr && it->second.textureData->preview != nullptr) {
        it->second.textureData->preview->setImage(m_context.editor->textures->registerTexture(gpuTexture));
    }
}

void NodeEditorPanel::destroyNodeRows(uint32_t nodeId)
{
    NodeView &view = m_nodes[nodeId];
    for (uint32_t pinId : view.pinIds) {
        m_pins.remove(pinId);
    }
    view.pinIds.clear();
    view.valueFields.clear();
    if (view.textureData != nullptr) {
        view.textureData->preview = nullptr;
    }
    for (Amethyst::UIObject *row : view.rows) {
        view.body->removeChild(row);
    }
    view.rows.clear();
}

void NodeEditorPanel::changeNodeType(uint32_t nodeId, Rapture::GraphNodeType newType)
{
    NodeView &view = m_nodes[nodeId];
    if (view.type == newType) {
        return;
    }

    // where each wire touches this node, so it can be moved onto the rebuilt pins
    struct Touch {
        size_t wireIndex;
        bool isOutput;
        uint32_t slot;
        Rapture::PinType type;
        uint32_t farPin;
    };
    std::vector<Touch> touches;
    for (size_t i = 0; i < m_connections.size(); ++i) {
        const WireView &wire = m_connections[i];
        const PinView &src = m_pins[wire.srcPinId];
        const PinView &dst = m_pins[wire.dstPinId];
        if (src.nodeId == nodeId) {
            touches.push_back({i, true, src.slotIndex, src.type, wire.dstPinId});
        } else if (dst.nodeId == nodeId) {
            touches.push_back({i, false, dst.slotIndex, dst.type, wire.srcPinId});
        }
    }

    view.type = newType;
    destroyNodeRows(nodeId);
    buildNodeRows(nodeId);

    // a wire moves onto the same slot when it still carries the same type, and goes otherwise
    std::vector<size_t> dropped;
    std::vector<uint32_t> farPins;
    for (const Touch &touch : touches) {
        WireView &wire = m_connections[touch.wireIndex];
        m_wireLayer->removeChild(wire.spline);
        wire.spline = nullptr;

        const uint32_t newPin = findPin(nodeId, touch.isOutput, touch.slot);
        if (newPin == INVALID_PIN || m_pins[newPin].type != touch.type) {
            dropped.push_back(touch.wireIndex);
            farPins.push_back(touch.farPin);
            continue;
        }
        if (touch.isOutput) {
            wire.srcPinId = newPin;
        } else {
            wire.dstPinId = newPin;
        }
        wire.spline = createWireCurve(wire.srcPinId, wire.dstPinId);
    }

    std::sort(dropped.begin(), dropped.end(), std::greater<size_t>());
    for (size_t index : dropped) {
        m_connections.erase(m_connections.begin() + static_cast<std::ptrdiff_t>(index));
    }
    for (uint32_t pinId : farPins) {
        applyPinState(pinId);
    }
    for (uint32_t pinId : view.pinIds) {
        applyPinState(pinId);
    }

    m_onNodeEdited.fire(nodeId);
}

void NodeEditorPanel::connectPins(uint32_t srcNode, uint32_t srcSlot, uint32_t dstNode, uint32_t dstSlot)
{
    const uint32_t outPin = findPin(srcNode, true, srcSlot);
    const uint32_t inPin = findPin(dstNode, false, dstSlot);
    if (outPin == INVALID_PIN || inPin == INVALID_PIN) {
        return;
    }
    createWire(outPin, inPin);
}

bool NodeEditorPanel::isInputConnected(uint32_t nodeId, uint32_t slotIndex) const
{
    const uint32_t pinId = findPin(nodeId, false, slotIndex);
    return pinId != INVALID_PIN && isPinConnected(pinId);
}

bool NodeEditorPanel::isPinConnected(uint32_t pinId) const
{
    return std::any_of(m_connections.begin(), m_connections.end(),
                       [pinId](const WireView &wire) { return wire.srcPinId == pinId || wire.dstPinId == pinId; });
}

uint32_t NodeEditorPanel::findPinAt(Amethyst::vec2 screenPos) const
{
    uint32_t best = INVALID_PIN;
    float bestDistance = PIN_HIT_RADIUS * PIN_HIT_RADIUS;
    m_pins.forEach([&](uint32_t id, const PinView &pin) {
        if (pin.socket == nullptr) {
            return;
        }
        const Amethyst::vec2 offset = pin.socket->absolutePosition + pin.socket->absoluteSize * 0.5f - screenPos;
        const float distance = offset.x * offset.x + offset.y * offset.y;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = id;
        }
    });
    return best;
}

uint32_t NodeEditorPanel::findPin(uint32_t nodeId, bool isOutput, uint32_t slotIndex) const
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return INVALID_PIN;
    }
    for (uint32_t pinId : it->second.pinIds) {
        const PinView &pin = m_pins[pinId];
        if (pin.isOutput == isOutput && pin.slotIndex == slotIndex) {
            return pinId;
        }
    }
    return INVALID_PIN;
}

bool NodeEditorPanel::canConnect(uint32_t a, uint32_t b) const
{
    if (a == b || !m_pins.isLive(a) || !m_pins.isLive(b)) {
        return false;
    }
    const PinView &first = m_pins[a];
    const PinView &second = m_pins[b];
    return first.nodeId != second.nodeId && first.type == second.type && first.isOutput != second.isOutput;
}

void NodeEditorPanel::beginConnection(uint32_t pinId, Amethyst::vec2 screenPos)
{
    // dragging off a connected input picks its wire up, to drop it or move it onto another input
    if (!m_pins[pinId].isOutput) {
        auto wire = std::find_if(m_connections.begin(), m_connections.end(), [pinId](const WireView &w) { return w.dstPinId == pinId; });
        if (wire != m_connections.end()) {
            const uint32_t sourcePin = wire->srcPinId;
            m_wireLayer->removeChild(wire->spline);
            m_connections.erase(wire);
            applyPinState(pinId);
            applyPinState(sourcePin);
            m_onNodeEdited.fire(m_pins[pinId].nodeId);
            pinId = sourcePin;
        }
    }

    m_connecting = true;
    m_connectSrcPinId = pinId;
    setHoverPin(INVALID_PIN);

    const PinView &source = m_pins[pinId];
    m_dragWire = m_wireLayer->add<Amethyst::Spline>();
    m_dragWire->addClass("graph-wire");
    m_dragWire->setSplineProperties({
        .type = Amethyst::CurveType::CUBIC,
        .color = Amethyst::Color4(PinType_color(source.type), 1.0f),
        .showKnots = false,
    });
    m_dragWire->setKnots(std::vector<Amethyst::vec2>(4, Amethyst::vec2(0.0f)));
    m_dragWire->attachKnot(0, source.socket, Amethyst::vec2(0.5f));
    m_dragWire->attachKnot(1, source.socket, Amethyst::vec2(0.5f), s_wireBendFor(source.isOutput));
    updateConnectionDrag(screenPos);

    if (auto *window = m_canvas->getWindow()) {
        window->captureMouse(m_canvas);
    }
}

void NodeEditorPanel::updateConnectionDrag(Amethyst::vec2 screenPos)
{
    // the wire sits at the wire layer's origin, and has no layout of its own yet on the frame it is created
    const Amethyst::vec2 cursor = screenPos - m_wireLayer->absolutePosition;
    const Amethyst::vec2 cursorBend = -s_wireBendFor(m_pins[m_connectSrcPinId].isOutput);
    // the first two knots follow the source pin, so only the cursor's end is placed here
    std::vector<Amethyst::vec2> knots = m_dragWire->getKnots();
    knots[2] = cursor + cursorBend;
    knots[3] = cursor;
    m_dragWire->setKnots(std::move(knots));

    uint32_t target = findPinAt(screenPos);
    if (target != INVALID_PIN && !canConnect(m_connectSrcPinId, target)) {
        target = INVALID_PIN;
    }
    setHoverPin(target);
}

void NodeEditorPanel::finishConnection(Amethyst::vec2 screenPos)
{
    const uint32_t target = findPinAt(screenPos);
    setHoverPin(INVALID_PIN);

    m_wireLayer->removeChild(m_dragWire);
    m_dragWire = nullptr;
    m_connecting = false;
    if (auto *window = m_canvas->getWindow()) {
        window->releaseMouse(m_canvas);
    }

    if (target != INVALID_PIN && canConnect(m_connectSrcPinId, target)) {
        const bool sourceIsOutput = m_pins[m_connectSrcPinId].isOutput;
        createWire(sourceIsOutput ? m_connectSrcPinId : target, sourceIsOutput ? target : m_connectSrcPinId);
        m_onNodeEdited.fire(m_pins[sourceIsOutput ? target : m_connectSrcPinId].nodeId);
    }
}

void NodeEditorPanel::createWire(uint32_t outPinId, uint32_t inPinId)
{
    // an input holds at most one wire, so whatever fed it before is dropped
    std::vector<uint32_t> replacedSources;
    std::erase_if(m_connections, [&](const WireView &wire) {
        if (wire.dstPinId != inPinId) {
            return false;
        }
        m_wireLayer->removeChild(wire.spline);
        replacedSources.push_back(wire.srcPinId);
        return true;
    });

    m_connections.push_back({.srcPinId = outPinId, .dstPinId = inPinId, .spline = createWireCurve(outPinId, inPinId)});

    for (uint32_t pinId : replacedSources) {
        applyPinState(pinId);
    }
    applyPinState(outPinId);
    applyPinState(inPinId);
}

Amethyst::Spline *NodeEditorPanel::createWireCurve(uint32_t from, uint32_t to)
{
    const PinView &source = m_pins[from];
    const PinView &target = m_pins[to];

    auto *spline = m_wireLayer->add<Amethyst::Spline>();
    spline->addClass("graph-wire");
    spline->setSplineProperties({
        .type = Amethyst::CurveType::CUBIC,
        .color = Amethyst::Color4(PinType_color(source.type), 1.0f),
        .showKnots = false,
    });
    spline->setKnots(std::vector<Amethyst::vec2>(4, Amethyst::vec2(0.0f)));
    spline->attachKnot(0, source.socket, Amethyst::vec2(0.5f));
    spline->attachKnot(1, source.socket, Amethyst::vec2(0.5f), s_wireBendFor(source.isOutput));
    spline->attachKnot(2, target.socket, Amethyst::vec2(0.5f), s_wireBendFor(target.isOutput));
    spline->attachKnot(3, target.socket, Amethyst::vec2(0.5f));
    return spline;
}

void NodeEditorPanel::applyPinState(uint32_t pinId)
{
    PinView &pin = m_pins[pinId];
    const bool connected = isPinConnected(pinId);
    if (pin.socket != nullptr) {
        const Amethyst::Color3 color = PinType_color(pin.type);
        pin.socket->setBaseStyleProperties({
            .backgroundColor = color,
            .backgroundTransparency = connected ? 0.0f : 1.0f,
            .borderColor = color,
        });
    }
    if (pin.inlineField != nullptr) {
        pin.inlineField->setBaseProperties({.visible = !connected});
    }
}

void NodeEditorPanel::setHoverPin(uint32_t pinId)
{
    if (pinId == m_hoverPinId) {
        return;
    }
    if (m_hoverPinId != INVALID_PIN && m_pins.isLive(m_hoverPinId)) {
        m_pins[m_hoverPinId].socket->setBaseProperties({.size = Amethyst::UDim2::fromOffset(PIN_SOCKET_SIZE, PIN_SOCKET_SIZE)});
    }
    if (pinId != INVALID_PIN) {
        m_pins[pinId].socket->setBaseProperties({.size = Amethyst::UDim2::fromOffset(PIN_SOCKET_TARGET_SIZE, PIN_SOCKET_TARGET_SIZE)});
    }
    m_hoverPinId = pinId;
}

void NodeEditorPanel::selectNode(uint32_t nodeId, bool additive)
{
    if (!additive) {
        clearSelection();
    }
    m_selectedNodes.insert(nodeId);
    s_setGuiStateFlag(*m_nodes[nodeId].frame, Amethyst::GUI_STATE_ACTIVE, true);
    setPrimaryNode(nodeId);
}

void NodeEditorPanel::clearSelection()
{
    for (uint32_t nodeId : m_selectedNodes) {
        auto it = m_nodes.find(nodeId);
        if (it != m_nodes.end()) {
            s_setGuiStateFlag(*it->second.frame, Amethyst::GUI_STATE_ACTIVE, false);
        }
    }
    m_selectedNodes.clear();
    setPrimaryNode(0);
}

void NodeEditorPanel::setPrimaryNode(uint32_t nodeId)
{
    if (m_primaryNodeId == nodeId) {
        return;
    }
    m_primaryNodeId = nodeId;
    m_onPrimaryNodeChanged.fire(nodeId);
}

void NodeEditorPanel::showNodeMenu(uint32_t nodeId, Amethyst::vec2 screenPos)
{
    // right clicking a node outside the selection makes it what the menu acts on
    if (m_selectedNodes.count(nodeId) == 0) {
        selectNode(nodeId, false);
    }

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(Amethyst::makeActionItem("Delete", [this]() { deleteSelection(); }));
    m_nodeMenu->setItems(std::move(items));
    m_nodeMenu->showAt(screenPos);
}

void NodeEditorPanel::deleteNode(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return;
    }

    std::vector<uint32_t> farPins;
    std::erase_if(m_connections, [&](const WireView &wire) {
        const bool srcTouches = m_pins[wire.srcPinId].nodeId == nodeId;
        const bool dstTouches = m_pins[wire.dstPinId].nodeId == nodeId;
        if (!srcTouches && !dstTouches) {
            return false;
        }
        farPins.push_back(srcTouches ? wire.dstPinId : wire.srcPinId);
        m_wireLayer->removeChild(wire.spline);
        return true;
    });

    destroyNodeRows(nodeId);
    m_content->removeChild(it->second.frame);
    m_nodes.erase(it);
    m_selectedNodes.erase(nodeId);
    if (m_primaryNodeId == nodeId) {
        setPrimaryNode(0);
    }

    for (uint32_t pinId : farPins) {
        if (m_pins.isLive(pinId)) {
            applyPinState(pinId);
            m_onNodeEdited.fire(m_pins[pinId].nodeId);
        }
    }
}

void NodeEditorPanel::deleteSelection()
{
    std::vector<uint32_t> targets(m_selectedNodes.begin(), m_selectedNodes.end());
    for (uint32_t nodeId : targets) {
        deleteNode(nodeId);
    }
    m_onNodeEdited.fire(0);
}

void NodeEditorPanel::clearGraph()
{
    std::vector<uint32_t> ids;
    ids.reserve(m_nodes.size());
    for (const auto &[nodeId, view] : m_nodes) {
        ids.push_back(nodeId);
    }
    for (uint32_t nodeId : ids) {
        deleteNode(nodeId);
    }
    m_nextNodeId = 1;
}

std::string_view NodeEditorPanel::getNodeTitle(uint32_t nodeId) const
{
    return m_nodes.at(nodeId).title;
}

GraphCategory NodeEditorPanel::getNodeCategory(uint32_t nodeId) const
{
    return m_nodes.at(nodeId).category;
}

std::vector<std::string> NodeEditorPanel::getNodeVariantLabels(uint32_t nodeId) const
{
    std::vector<std::string> labels;
    if (const NodeVariantGroup *group = s_variantGroupFor(m_nodes.at(nodeId).type)) {
        for (const NodeVariant &variant : group->variants) {
            labels.emplace_back(variant.label);
        }
    }
    return labels;
}

std::string_view NodeEditorPanel::getNodeVariantLabel(uint32_t nodeId) const
{
    const Rapture::GraphNodeType type = m_nodes.at(nodeId).type;
    if (const NodeVariantGroup *group = s_variantGroupFor(type)) {
        for (const NodeVariant &variant : group->variants) {
            if (variant.type == type) {
                return variant.label;
            }
        }
    }
    return {};
}

void NodeEditorPanel::setNodeVariant(uint32_t nodeId, size_t variantIndex)
{
    const NodeVariantGroup *group = s_variantGroupFor(m_nodes.at(nodeId).type);
    if (group == nullptr || variantIndex >= group->variants.size()) {
        return;
    }
    changeNodeType(nodeId, group->variants[variantIndex].type);
    m_nodes[nodeId].variantDropdown->setText(group->variants[variantIndex].label);
}

bool NodeEditorPanel::samplesTextureAsset(uint32_t nodeId) const
{
    const NodeView &view = m_nodes.at(nodeId);
    return view.textureData != nullptr && view.textureData->kind == TextureNodeKind::ASSET;
}

Rapture::AssetHandle NodeEditorPanel::getNodeTexture(uint32_t nodeId) const
{
    const NodeView &view = m_nodes.at(nodeId);
    if (view.textureData == nullptr || !view.textureData->texture) {
        return Rapture::INVALID_ASSET_HANDLE;
    }
    return view.textureData->texture.get()->handle();
}

void NodeEditorPanel::setNodeTexture(uint32_t nodeId, Rapture::AssetHandle handle)
{
    Rapture::Ref<Rapture::ATexture> texture = Rapture::AssetManager::getAsset<Rapture::ATexture>(handle);
    if (!texture) {
        return;
    }
    setImageTexture(nodeId, std::move(texture));
    m_onNodeEdited.fire(nodeId);
}

std::vector<NodeEditorPanel::NodeValueRow> NodeEditorPanel::getNodeValueRows(uint32_t nodeId)
{
    std::vector<NodeValueRow> rows;
    const NodeView &view = m_nodes.at(nodeId);
    // an editor-only texture node is lowered at compile time, its pins carry no authored values
    if (view.textureData != nullptr && view.type == GNT::NONE) {
        return rows;
    }

    const bool constant = s_isConstantType(view.type);
    for (uint32_t pinId : view.pinIds) {
        PinView &pin = m_pins[pinId];
        if (pin.isOutput || pin.value == nullptr || pin.type == Rapture::PinType::TEXTURE) {
            continue;
        }
        rows.push_back({
            .label = constant ? std::string("Value") : pin.name,
            .values = std::span<float>(&pin.value->v4[0], Rapture::graph_pinTypeComponents(pin.type)),
            .wholeNumbers = pin.type == Rapture::PinType::INT,
            .connected = isPinConnected(pinId),
        });
    }
    return rows;
}

void NodeEditorPanel::refreshNodeFields(uint32_t nodeId)
{
    for (Amethyst::UIObject *field : m_nodes.at(nodeId).valueFields) {
        field->markDirty();
    }
}

Rapture::MaterialGraph NodeEditorPanel::buildGraph() const
{
    Rapture::MaterialGraph graph;

    uint32_t outputNodeId = 0;
    for (const auto &[nodeId, view] : m_nodes) {
        if (view.type == GNT::SURFACE_OUTPUT) {
            outputNodeId = nodeId;
            break;
        }
    }
    if (outputNodeId == 0) {
        RP_WARN("The graph has no PBR Surface node, nothing to compile");
        return graph;
    }

    // walk back from the output so only nodes feeding it are kept
    std::unordered_set<uint32_t> reachable;
    std::vector<uint32_t> stack{outputNodeId};
    reachable.insert(outputNodeId);
    while (!stack.empty()) {
        uint32_t current = stack.back();
        stack.pop_back();
        for (const auto &wire : m_connections) {
            if (m_pins[wire.dstPinId].nodeId != current) {
                continue;
            }
            uint32_t source = m_pins[wire.srcPinId].nodeId;
            if (reachable.insert(source).second) {
                stack.push_back(source);
            }
        }
    }

    graph.name = "EditorGraph";
    graph.outputNodeId = outputNodeId;

    // A texture node is editor sugar: it lowers to a TEXTURE_SAMPLE, a synthesised TEXCOORD when its
    // uv is unconnected, and a SPLIT_VEC4 when its alpha is used. These maps redirect a lowered
    // node's editor pins onto the primitive pin that ends up carrying them.
    struct Endpoint {
        uint32_t node;
        uint32_t pin;
    };
    std::unordered_map<uint64_t, Endpoint> outputRemap;
    std::unordered_map<uint64_t, Endpoint> inputRemap;
    auto pinKey = [](uint32_t node, uint32_t pin) { return (static_cast<uint64_t>(node) << 32) | pin; };

    // synthesised primitives take fresh ids above every editor id
    uint32_t nextSynthId = m_nextNodeId;

    auto outputUsed = [&](uint32_t node, uint32_t slot) {
        return std::any_of(m_connections.begin(), m_connections.end(), [&](const WireView &wire) {
            const PinView &pin = m_pins[wire.srcPinId];
            return pin.nodeId == node && pin.slotIndex == slot;
        });
    };

    for (uint32_t nodeId : reachable) {
        const NodeView &view = m_nodes.at(nodeId);

        if (view.textureData != nullptr && view.type == GNT::NONE) {
            uint32_t sampleId = nextSynthId++;
            Rapture::GraphNode sample;
            sample.id = sampleId;
            sample.type = GNT::TEXTURE_SAMPLE;
            sample.inputValues.resize(2);
            sample.inputTextures.resize(1);
            sample.inputTextures[0] = view.textureData->texture;
            graph.nodes.push_back(std::move(sample));

            // uv is the sample's second input, fed by a synthesised TEXCOORD when unconnected
            inputRemap[pinKey(nodeId, 0)] = {sampleId, 1};
            if (!isInputConnected(nodeId, 0)) {
                uint32_t texcoordId = nextSynthId++;
                Rapture::GraphNode texcoord;
                texcoord.id = texcoordId;
                texcoord.type = GNT::TEXCOORD;
                graph.nodes.push_back(std::move(texcoord));
                graph.connections.push_back({.srcNode = texcoordId, .srcPin = 0, .dstNode = sampleId, .dstPin = 1});
            }

            // Color reads the sample directly, the vec4 to vec3 coercion already takes rgb
            outputRemap[pinKey(nodeId, 0)] = {sampleId, 0};

            // Alpha needs the w channel, so the sample is split only when something reads it
            if (outputUsed(nodeId, 1)) {
                uint32_t splitId = nextSynthId++;
                Rapture::GraphNode split;
                split.id = splitId;
                split.type = GNT::SPLIT_VEC4;
                split.inputValues.resize(1);
                graph.nodes.push_back(std::move(split));
                graph.connections.push_back({.srcNode = sampleId, .srcPin = 0, .dstNode = splitId, .dstPin = 0});
                outputRemap[pinKey(nodeId, 1)] = {splitId, 3};
            }
            continue;
        }

        Rapture::GraphNode node;
        node.id = nodeId;
        node.type = view.type;

        // every input's authored value is carried across, the compiler ignores it on wired pins
        const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(node.type);
        if (def != nullptr) {
            node.inputValues.resize(def->inputs.size());
            for (uint32_t i = 0; i < def->inputs.size(); ++i) {
                uint32_t pinId = findPin(nodeId, false, i);
                if (pinId != INVALID_PIN && m_pins[pinId].value != nullptr) {
                    node.inputValues[i] = *m_pins[pinId].value;
                }
            }
        }

        if (view.type == GNT::TEXTURE_SAMPLE && view.textureData != nullptr && view.textureData->texture.get() != nullptr) {
            node.inputTextures.resize(1);
            node.inputTextures[0] = view.textureData->texture;
        }

        graph.nodes.push_back(std::move(node));
    }

    // a wire into a reachable node always has a reachable source, so every wire ending inside is kept
    for (const auto &wire : m_connections) {
        const PinView &src = m_pins[wire.srcPinId];
        const PinView &dst = m_pins[wire.dstPinId];
        if (reachable.count(dst.nodeId) == 0) {
            continue;
        }

        Endpoint from{src.nodeId, src.slotIndex};
        if (auto outIt = outputRemap.find(pinKey(src.nodeId, src.slotIndex)); outIt != outputRemap.end()) {
            from = outIt->second;
        }
        Endpoint to{dst.nodeId, dst.slotIndex};
        if (auto inIt = inputRemap.find(pinKey(dst.nodeId, dst.slotIndex)); inIt != inputRemap.end()) {
            to = inIt->second;
        }
        graph.connections.push_back({.srcNode = from.node, .srcPin = from.pin, .dstNode = to.node, .dstPin = to.pin});
    }

    return graph;
}

void NodeEditorPanel::compileGraph()
{
    if (m_selectedGraphId == UINT32_MAX) {
        RP_WARN("No material is open to compile into");
        m_onCompiled.fire(false, m_nodes.size());
        return;
    }

    Rapture::MaterialGraph graph = buildGraph();
    if (graph.nodes.empty()) {
        m_onCompiled.fire(false, m_nodes.size());
        return;
    }

    auto &graphs = Rapture::MaterialManager::getSurfaceGraphManager();
    if (!graphs.updateGraph(m_selectedGraphId, graph)) {
        RP_ERROR("Graph compile failed");
        m_onCompiled.fire(false, m_nodes.size());
        return;
    }

    auto generatedDir = Rapture::EnginePaths::shaderDirectory() / "glsl/generated";
    graphs.writeGeneratedFiles(generatedDir);
    graphs.notifyShadersOfRegeneration();
    m_onCompiled.fire(true, m_nodes.size());
}
