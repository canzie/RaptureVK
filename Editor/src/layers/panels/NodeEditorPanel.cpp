#include "NodeEditorPanel.h"

#include "Icons.h"
#include "layers/panels/components/tab_layouts.h"

#include "materials/graph/MaterialGraphCompiler.h"
#include "materials/graph/NodeRegistry.h"
#include "materials/graph/SurfaceGraphManager.h"

#include "asset_manager/Asset.h"
#include "asset_manager/AssetManager.h"
#include "generators/textures/ProceduralTextures.h"
#include "logging/Log.h"
#include "materials/Material.h"
#include "materials/MaterialData.h"
#include "materials/MaterialInstance.h"
#include "materials/MaterialParameters.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

#include <components/common.h>
#include <components/drag.h>
#include <components/dropdown.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/image_label.h>
#include <components/shape.h>
#include <components/text_button.h>
#include <components/text_label.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#define COL_NODE_BODY  Amethyst::Color3::fromHex(0x303030)
#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

#define COL_BG Amethyst::Color3::fromHex(0x1a1a1a)

#define COL_CAT_INPUT     Amethyst::Color3::fromHex(0x3a6ea5)
#define COL_CAT_UTILITIES Amethyst::Color3::fromHex(0x555b66)
#define COL_CAT_GEOMETRY  Amethyst::Color3::fromHex(0xa5613a)
#define COL_CAT_COLOR     Amethyst::Color3::fromHex(0xa59a3a)
#define COL_CAT_TEXTURE   Amethyst::Color3::fromHex(0x7a3a6e)
#define COL_CAT_OUTPUT    Amethyst::Color3::fromHex(0x3a8a4f)
#define COL_CAT_DEFAULT   Amethyst::Color3::fromHex(0x394150)

#define COL_PIN_FLOAT  Amethyst::Color3::fromHex(0xa1a1a1)
#define COL_PIN_INT    Amethyst::Color3::fromHex(0x4f9d55)
#define COL_PIN_VEC2   Amethyst::Color3::fromHex(0x5fb0c9)
#define COL_PIN_VEC3   Amethyst::Color3::fromHex(0x6b6bd6)
#define COL_PIN_VEC4   Amethyst::Color3::fromHex(0xd0b24a)
#define COL_PIN_TEX    Amethyst::Color3::fromHex(0xb05fa5)
#define COL_PIN_HOVER  Amethyst::Color3::fromHex(0xffffff)
#define COL_PIN_BORDER Amethyst::Color3::fromHex(0x2b2b2b)

#define COL_NODE_SELECTED         Amethyst::Color3::fromHex(0xc07a2c)
#define COL_NODE_SELECTED_PRIMARY Amethyst::Color3::fromHex(0xf5f0e6)

static constexpr float NODE_WIDTH = 168.0f;
static constexpr float NODE_HEADER_HEIGHT = 26.0f;
static constexpr float NODE_ROW_HEIGHT = 22.0f;
static constexpr float NODE_PADDING = 8.0f;
static constexpr float NODE_PIN_SIZE = 12.0f;
static constexpr float NODE_PIN_BORDER = 1.5f;
static constexpr float NODE_BORDER = 1.5f;
static constexpr float WIRE_THICKNESS = 3.5f;
static constexpr float PIN_HIT_RADIUS = 11.0f;

/**
 * @brief Header/accent colour for a top level menu category
 * @param category The top level category label
 * @return The category colour, or a neutral default for an unknown category
 */
static Amethyst::Color3 s_categoryColor(std::string_view category)
{
    if (category == "Input") {
        return COL_CAT_INPUT;
    }
    if (category == "Utilities") {
        return COL_CAT_UTILITIES;
    }
    if (category == "Geometry") {
        return COL_CAT_GEOMETRY;
    }
    if (category == "Color") {
        return COL_CAT_COLOR;
    }
    if (category == "Texture") {
        return COL_CAT_TEXTURE;
    }
    if (category == "Output") {
        return COL_CAT_OUTPUT;
    }
    return COL_CAT_DEFAULT;
}

/**
 * @brief Socket colour for a pin data type
 * @param type The pin type
 * @return The colour used for that type's pin socket
 */
static Amethyst::Color3 s_pinColor(Rapture::PinType type)
{
    switch (type) {
    case Rapture::PinType::FLOAT:
        return COL_PIN_FLOAT;
    case Rapture::PinType::INT:
        return COL_PIN_INT;
    case Rapture::PinType::VEC2:
        return COL_PIN_VEC2;
    case Rapture::PinType::VEC3:
        return COL_PIN_VEC3;
    case Rapture::PinType::VEC4:
        return COL_PIN_VEC4;
    case Rapture::PinType::TEXTURE:
        return COL_PIN_TEX;
    }
    return COL_PIN_FLOAT;
}

/**
 * @brief A node type the catalog can spawn, paired with its menu label
 */
struct NodeCatalogEntry {
    const char *label;
    Rapture::GraphNodeType type;
    NodeEditorPanel::TextureNodeKind textureKind = NodeEditorPanel::TextureNodeKind::NONE;
};

/**
 * @brief A branch of the add-node menu tree: leaf entries plus nested categories
 */
struct NodeCatalogCategory {
    const char *label;
    std::vector<NodeCatalogEntry> entries;
    std::vector<NodeCatalogCategory> subcategories;
};

using GNT = Rapture::GraphNodeType;

static const std::vector<NodeCatalogCategory> &s_nodeCatalog()
{
    static const std::vector<NodeCatalogCategory> catalog = {
        {"Input",
         {{"Texture Sample", GNT::TEXTURE_SAMPLE}},
         {
             {"Constant",
              {{"Float", GNT::CONSTANT_FLOAT},
               {"Integer", GNT::CONSTANT_INT},
               {"Vector 2", GNT::CONSTANT_VEC2},
               {"Vector 3", GNT::CONSTANT_VEC3},
               {"Vector 4", GNT::CONSTANT_VEC4}},
              {}},
         }},
        {"Utilities",
         {},
         {
             {"Math",
              {},
              {
                  {"Float",
                   {{"Add", GNT::ADD_FLOAT},
                    {"Subtract", GNT::SUBTRACT_FLOAT},
                    {"Multiply", GNT::MULTIPLY_FLOAT},
                    {"Divide", GNT::DIVIDE_FLOAT},
                    {"Absolute", GNT::ABS_FLOAT},
                    {"Minimum", GNT::MIN_FLOAT},
                    {"Maximum", GNT::MAX_FLOAT},
                    {"Clamp", GNT::CLAMP_FLOAT},
                    {"Saturate", GNT::SATURATE_FLOAT},
                    {"Mix", GNT::MIX_FLOAT},
                    {"Step", GNT::STEP_FLOAT},
                    {"Smoothstep", GNT::SMOOTHSTEP_FLOAT},
                    {"Fract", GNT::FRACT_FLOAT},
                    {"Power", GNT::POWER_FLOAT},
                    {"Square Root", GNT::SQRT_FLOAT},
                    {"Sine", GNT::SIN_FLOAT},
                    {"Cosine", GNT::COS_FLOAT},
                    {"Remap", GNT::REMAP_FLOAT}},
                   {}},
                  {"Integer",
                   {{"Add", GNT::ADD_INT},
                    {"Subtract", GNT::SUBTRACT_INT},
                    {"Multiply", GNT::MULTIPLY_INT},
                    {"Divide", GNT::DIVIDE_INT},
                    {"Absolute", GNT::ABS_INT},
                    {"Minimum", GNT::MIN_INT},
                    {"Maximum", GNT::MAX_INT},
                    {"Clamp", GNT::CLAMP_INT}},
                   {}},
                  {"Vector",
                   {{"Add", GNT::ADD_VEC3},
                    {"Subtract", GNT::SUBTRACT_VEC3},
                    {"Multiply", GNT::MULTIPLY_VEC3},
                    {"Divide", GNT::DIVIDE_VEC3},
                    {"Absolute", GNT::ABS_VEC3},
                    {"Minimum", GNT::MIN_VEC3},
                    {"Maximum", GNT::MAX_VEC3},
                    {"Clamp", GNT::CLAMP_VEC3},
                    {"Saturate", GNT::SATURATE_VEC3},
                    {"Mix", GNT::MIX_VEC3},
                    {"Step", GNT::STEP_VEC3},
                    {"Smoothstep", GNT::SMOOTHSTEP_VEC3},
                    {"Fract", GNT::FRACT_VEC3},
                    {"Power", GNT::POWER_VEC3},
                    {"Square Root", GNT::SQRT_VEC3},
                    {"Sine", GNT::SIN_VEC3},
                    {"Cosine", GNT::COS_VEC3},
                    {"Dot Product", GNT::DOT_VEC3},
                    {"Cross Product", GNT::CROSS_VEC3},
                    {"Normalize", GNT::NORMALIZE_VEC3},
                    {"Length", GNT::LENGTH_VEC3},
                    {"Distance", GNT::DISTANCE_VEC3}},
                   {}},
              }},
             {"Vector",
              {{"Combine 2", GNT::COMBINE_VEC2},
               {"Combine 3", GNT::COMBINE_VEC3},
               {"Combine 4", GNT::COMBINE_VEC4},
               {"Split 2", GNT::SPLIT_VEC2},
               {"Split 3", GNT::SPLIT_VEC3},
               {"Split 4", GNT::SPLIT_VEC4}},
              {}},
         }},
        {"Geometry",
         {{"Position", GNT::POSITION},
          {"Normal", GNT::NORMAL},
          {"Tangent", GNT::TANGENT},
          {"Bitangent", GNT::BITANGENT},
          {"Texture Coordinate", GNT::TEXCOORD},
          {"Normal Map", GNT::NORMAL_MAP}},
         {}},
        {"Color", {{"Luminance", GNT::LUMINANCE}}, {}},
        {"Texture",
         {{"Image", GNT::NONE, NodeEditorPanel::TextureNodeKind::ASSET},
          {"White Noise", GNT::NONE, NodeEditorPanel::TextureNodeKind::WHITE_NOISE},
          {"Perlin Noise", GNT::NONE, NodeEditorPanel::TextureNodeKind::PERLIN_NOISE},
          {"Simplex Noise", GNT::NONE, NodeEditorPanel::TextureNodeKind::SIMPLEX_NOISE},
          {"Ridged Noise", GNT::NONE, NodeEditorPanel::TextureNodeKind::RIDGED_NOISE}},
         {}},
        {"Output", {{"PBR Surface", GNT::SURFACE_OUTPUT}}, {}},
    };
    return catalog;
}

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

/**
 * @brief The dropdown label for a type within its group
 * @param group The variant group
 * @param type The node type
 * @return The variant's label, or empty if the type is not in the group
 */
static std::string_view s_variantLabel(const NodeVariantGroup &group, Rapture::GraphNodeType type)
{
    for (const auto &variant : group.variants) {
        if (variant.type == type) {
            return variant.label;
        }
    }
    return {};
}

/**
 * @brief Whether a node type is a constant value source
 * @param type The node type
 * @return True for the CONSTANT_* family
 */
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

using SpawnFn = std::function<void(Rapture::GraphNodeType, std::string_view, Amethyst::Color3)>;
using TexSpawnFn = std::function<void(NodeEditorPanel::TextureNodeKind, std::string_view)>;

static Amethyst::ContextMenuItem s_categoryToMenuItem(const NodeCatalogCategory &category, const SpawnFn &spawn,
                                                      const TexSpawnFn &spawnTexture, Amethyst::Color3 color);

NodeEditorPanel::NodeEditorPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context) : Panel(context)
{
    Rapture::GraphDomainRegistry::registerBuiltins();

    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_canvas = nullptr;
        m_content = nullptr;
        m_wireLayer = nullptr;
        m_contextMenu = nullptr;
        m_dragWire = nullptr;
        m_materialBar = nullptr;
        m_materialDropdown = nullptr;
        m_connecting = false;
        m_selectedNodes.clear();
        m_primaryNodeId = 0;
    });
    m_root->name = "Node Editor";
    m_root->addClass("background-secondary");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupMaterialBar();
    setupCanvas();
    setupContextMenu();

    tabBar->addTab(std::move(root), iconTabLayout("Node Editor", Icons::SVG_MATERIAL));

    m_serializeListener =
        Rapture::ProjectEvents::onProjectSerialize().addListener([this](Rapture::WriteNode &root) { (void)root; });
}

NodeEditorPanel::~NodeEditorPanel()
{
    Rapture::ProjectEvents::onProjectSerialize().removeListener(m_serializeListener);

    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void NodeEditorPanel::setupCanvas()
{
    m_canvas = m_root->add<Amethyst::Frame>();
    m_canvas->name = "Canvas";
    m_canvas->setBaseProperties({
        .clipsDescendants = true,
        .position = Amethyst::UDim2::fromOffset(0.0f, MATERIAL_BAR_HEIGHT),
        .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -MATERIAL_BAR_HEIGHT),
    });
    m_canvas->setBaseStyleProperties({.backgroundColor = COL_BG, .backgroundTransparency = 0.0f});

    activateCanvasLayer();

    m_canvasBeganConn = m_canvas->onInputBeganCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputBegan(io); });
    m_canvasMovedConn = m_canvas->onInputChangedCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputChanged(io); });
    m_canvasEndedConn = m_canvas->onInputEndedCb.connect([this](const Amethyst::InputObject &io) { onCanvasInputEnded(io); });
}

void NodeEditorPanel::onCanvasInputBegan(const Amethyst::InputObject &io)
{
    if (io.type == Amethyst::InputType::MOUSE_BUTTON_2) {
        m_menuScreenPos = Amethyst::vec2(io.position.x, io.position.y);
        if (m_contextMenu != nullptr) {
            m_contextMenu->setItems(buildAddMenu());
            m_contextMenu->showAt(m_menuScreenPos);
        }
        return;
    }

    if (io.type == Amethyst::InputType::MOUSE_BUTTON_3) {
        m_panning = true;
        m_panLastMouse = Amethyst::vec2(io.position.x, io.position.y);
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
    Amethyst::vec2 mouse(io.position.x, io.position.y);

    if (m_panning) {
        m_pan += mouse - m_panLastMouse;
        m_panLastMouse = mouse;
        if (m_content != nullptr) {
            m_content->setBaseProperties({.position = Amethyst::UDim2::fromOffset(m_pan.x, m_pan.y)});
        }
        return;
    }

    if (m_connecting && m_content != nullptr) {
        updateConnectionDrag(mouse - m_content->absolutePosition);
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

    if (m_connecting && io.type == Amethyst::InputType::MOUSE_BUTTON_1 && m_content != nullptr) {
        Amethyst::vec2 mouse(io.position.x, io.position.y);
        finishConnection(mouse - m_content->absolutePosition);
    }
}

void NodeEditorPanel::setupContextMenu()
{
    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
    m_contextMenu->setContextMenuProperties({.itemHoverBackground = COL_MENU_HOVER});
}

void NodeEditorPanel::setupMaterialBar()
{
    m_materialBar = m_root->add<Amethyst::Frame>();
    m_materialBar->name = "Material Bar";
    m_materialBar->setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, MATERIAL_BAR_HEIGHT), .zIndex = 1});
    m_materialBar->addClass("background-tertiary");

    m_materialDropdown = m_materialBar->add<Amethyst::Dropdown>();
    m_materialDropdown->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(6.0f, 4.0f),
        .size = Amethyst::UDim2::fromOffset(220.0f, MATERIAL_BAR_HEIGHT - 8.0f),
    });
    m_materialDropdown->setText("Select material");
    rebuildMaterialList();

    auto *refresh = m_materialBar->add<Amethyst::TextButton>();
    refresh->setText("Refresh");
    refresh->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(232.0f, 4.0f),
        .size = Amethyst::UDim2::fromOffset(70.0f, MATERIAL_BAR_HEIGHT - 8.0f),
    });
    refresh->onMouseButton1ClickCb = [this]() {
        rebuildMaterialList();
        return Amethyst::EventResult::CONSUMED;
    };

    auto *newMaterial = m_materialBar->add<Amethyst::TextButton>();
    newMaterial->setText("New");
    newMaterial->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(308.0f, 4.0f),
        .size = Amethyst::UDim2::fromOffset(60.0f, MATERIAL_BAR_HEIGHT - 8.0f),
    });
    newMaterial->onMouseButton1ClickCb = [this]() {
        clearGraph();
        spawnNode(Rapture::GraphNodeType::SURFACE_OUTPUT, "PBR Surface", COL_CAT_OUTPUT, Amethyst::vec2(60.0f, 60.0f));
        m_materialDropdown->setText("New Material");
        return Amethyst::EventResult::CONSUMED;
    };
}

void NodeEditorPanel::rebuildMaterialList()
{
    if (m_materialDropdown == nullptr) {
        return;
    }
    std::vector<Amethyst::ContextMenuItem> items;
    for (Rapture::AssetHandle handle : Rapture::AssetManager::getVirtualAssetsByType(Rapture::AssetType::MATERIAL)) {
        std::string name = Rapture::AssetManager::getAssetMetadata(handle).getName();
        items.push_back(Amethyst::ContextMenuItem::action(name, [this, handle]() { selectMaterial(handle); }));
    }
    m_materialDropdown->setItems(std::move(items));
}

void NodeEditorPanel::selectMaterial(Rapture::AssetHandle handle)
{
    Rapture::AssetRef ref = Rapture::AssetManager::getAsset(handle);
    if (!ref) {
        return;
    }
    auto *material = ref.get()->getUnderlyingAsset<Rapture::MaterialInstance>();
    if (material == nullptr) {
        return;
    }
    std::shared_ptr<Rapture::BaseMaterial> base = material->getBaseMaterial();
    if (base == nullptr) {
        return;
    }
    uint32_t newGraphId = base->getGraphId();
    if (newGraphId != m_selectedGraphId) {
        bool hadPrevious = m_selectedGraphId != UINT32_MAX;
        if (hadPrevious) {
            stashGraph(m_selectedGraphId);
        }
        m_selectedGraphId = newGraphId;

        auto it = m_graphViews.find(newGraphId);
        if (it != m_graphViews.end()) {
            restoreGraph(it->second);
            m_graphViews.erase(it);
        } else {
            if (hadPrevious) {
                activateCanvasLayer();
            }
            loadGraph(base->getGraph());
        }
    }

    m_materialDropdown->setText(Rapture::AssetManager::getAssetMetadata(handle).getName());

    m_onMaterialSelectionChanged.fire(handle);
}

static const char *s_nodeDisplayName(Rapture::GraphNodeType type)
{
    switch (type) {
    case GNT::NONE:
        return "None";
    case GNT::POSITION:
        return "Position";
    case GNT::NORMAL:
        return "Normal";
    case GNT::TANGENT:
        return "Tangent";
    case GNT::BITANGENT:
        return "Bitangent";
    case GNT::TEXCOORD:
        return "Texture Coordinate";
    case GNT::CONSTANT_FLOAT:
        return "Float";
    case GNT::CONSTANT_INT:
        return "Integer";
    case GNT::CONSTANT_VEC2:
        return "Vector 2";
    case GNT::CONSTANT_VEC3:
        return "Vector 3";
    case GNT::CONSTANT_VEC4:
        return "Vector 4";
    case GNT::TEXTURE_SAMPLE:
        return "Texture Sample";
    case GNT::TEXTURE_WHITE_NOISE:
        return "White Noise";
    case GNT::TEXTURE_PERLIN:
        return "Perlin Noise";
    case GNT::TEXTURE_SIMPLEX:
        return "Simplex Noise";
    case GNT::TEXTURE_RIDGED:
        return "Ridged Noise";
    case GNT::ADD_FLOAT:
    case GNT::ADD_VEC3:
    case GNT::ADD_INT:
        return "Add";
    case GNT::SUBTRACT_FLOAT:
    case GNT::SUBTRACT_VEC3:
    case GNT::SUBTRACT_INT:
        return "Subtract";
    case GNT::MULTIPLY_FLOAT:
    case GNT::MULTIPLY_VEC3:
    case GNT::MULTIPLY_INT:
        return "Multiply";
    case GNT::DIVIDE_FLOAT:
    case GNT::DIVIDE_VEC3:
    case GNT::DIVIDE_INT:
        return "Divide";
    case GNT::ABS_FLOAT:
    case GNT::ABS_VEC3:
    case GNT::ABS_INT:
        return "Absolute";
    case GNT::MIN_FLOAT:
    case GNT::MIN_VEC3:
    case GNT::MIN_INT:
        return "Minimum";
    case GNT::MAX_FLOAT:
    case GNT::MAX_VEC3:
    case GNT::MAX_INT:
        return "Maximum";
    case GNT::CLAMP_FLOAT:
    case GNT::CLAMP_VEC3:
    case GNT::CLAMP_INT:
        return "Clamp";
    case GNT::SATURATE_FLOAT:
    case GNT::SATURATE_VEC3:
        return "Saturate";
    case GNT::MIX_FLOAT:
    case GNT::MIX_VEC3:
        return "Mix";
    case GNT::STEP_FLOAT:
    case GNT::STEP_VEC3:
        return "Step";
    case GNT::SMOOTHSTEP_FLOAT:
    case GNT::SMOOTHSTEP_VEC3:
        return "Smoothstep";
    case GNT::FRACT_FLOAT:
    case GNT::FRACT_VEC3:
        return "Fract";
    case GNT::POWER_FLOAT:
    case GNT::POWER_VEC3:
        return "Power";
    case GNT::SQRT_FLOAT:
    case GNT::SQRT_VEC3:
        return "Square Root";
    case GNT::SIN_FLOAT:
    case GNT::SIN_VEC3:
        return "Sine";
    case GNT::COS_FLOAT:
    case GNT::COS_VEC3:
        return "Cosine";
    case GNT::DOT_VEC3:
        return "Dot Product";
    case GNT::CROSS_VEC3:
        return "Cross Product";
    case GNT::NORMALIZE_VEC3:
        return "Normalize";
    case GNT::LENGTH_VEC3:
        return "Length";
    case GNT::DISTANCE_VEC3:
        return "Distance";
    case GNT::COMBINE_VEC2:
        return "Combine 2";
    case GNT::COMBINE_VEC3:
        return "Combine 3";
    case GNT::COMBINE_VEC4:
        return "Combine 4";
    case GNT::SPLIT_VEC2:
        return "Split 2";
    case GNT::SPLIT_VEC3:
        return "Split 3";
    case GNT::SPLIT_VEC4:
        return "Split 4";
    case GNT::NORMAL_MAP:
        return "Normal Map";
    case GNT::NORMAL_MAP_RG:
        return "Normal Map (RG)";
    case GNT::LUMINANCE:
        return "Luminance";
    case GNT::REMAP_FLOAT:
        return "Remap";
    case GNT::SURFACE_OUTPUT:
        return "PBR Surface";
    }
    return Rapture::Graph_nodeTypeName(type);
}

static bool s_categoryContainsType(const NodeCatalogCategory &category, Rapture::GraphNodeType type)
{
    for (const auto &entry : category.entries) {
        if (entry.textureKind == NodeEditorPanel::TextureNodeKind::NONE && entry.type == type) {
            return true;
        }
    }
    for (const auto &sub : category.subcategories) {
        if (s_categoryContainsType(sub, type)) {
            return true;
        }
    }
    return false;
}

static Amethyst::Color3 s_nodeCategoryColor(Rapture::GraphNodeType type)
{
    for (const auto &category : s_nodeCatalog()) {
        if (s_categoryContainsType(category, type)) {
            return s_categoryColor(category.label);
        }
    }
    return COL_CAT_DEFAULT;
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

        uint32_t spawnedId = spawnNode(node.type, s_nodeDisplayName(node.type), s_nodeCategoryColor(node.type), pos);
        if (spawnedId == 0) {
            continue;
        }
        idMap[node.id] = spawnedId;

        if (node.type == Rapture::GraphNodeType::TEXTURE_SAMPLE && !node.inputTextures.empty() &&
            node.inputTextures[0].get() != nullptr) {
            setImageTexture(spawnedId, node.inputTextures[0]);
        }

        for (uint32_t i = 0; i < node.inputValues.size(); ++i) {
            if (!node.inputValues[i].has_value()) {
                continue;
            }
            uint32_t pinId = findPin(spawnedId, false, i);
            if (pinId != INVALID_PIN && m_pins.isLive(pinId) && m_pins[pinId].value != nullptr) {
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

std::vector<Amethyst::ContextMenuItem> NodeEditorPanel::buildAddMenu()
{
    SpawnFn spawn = [this](Rapture::GraphNodeType type, std::string_view label, Amethyst::Color3 color) {
        if (m_content != nullptr) {
            spawnNode(type, label, color, m_menuScreenPos - m_content->absolutePosition);
        }
    };
    TexSpawnFn spawnTexture = [this](NodeEditorPanel::TextureNodeKind kind, std::string_view label) {
        if (m_content != nullptr) {
            spawnTextureNode(kind, label, m_menuScreenPos - m_content->absolutePosition);
        }
    };

    std::vector<Amethyst::ContextMenuItem> addItems;
    for (const auto &category : s_nodeCatalog()) {
        addItems.push_back(s_categoryToMenuItem(category, spawn, spawnTexture, s_categoryColor(category.label)));
    }

    std::vector<Amethyst::ContextMenuItem> root;
    root.push_back(Amethyst::ContextMenuItem::submenu("Add", std::move(addItems)));
    root.push_back(Amethyst::ContextMenuItem::action("Paste", [] {}).withEnabled(false));
    return root;
}

Amethyst::Frame *NodeEditorPanel::createNodeShell(uint32_t nodeId, Amethyst::vec2 canvasPos, std::string_view headerText,
                                                  Amethyst::Color3 headerColor)
{
    auto *node = m_content->add<Amethyst::Frame>();
    node->name = "Node " + std::to_string(nodeId);
    // Do not clip: pin sockets straddle the node border. The size is set once the pins are laid out.
    node->setBaseProperties({
        .clipsDescendants = false,
        .position = Amethyst::UDim2::fromOffset(canvasPos.x, canvasPos.y),
        .zIndex = 1,
    });
    node->setBaseStyleProperties({
        .backgroundColor = COL_NODE_BODY,
        .borderMode = Amethyst::BorderMode::OUTLINE,
        .borderPixelSize = NODE_BORDER,
        .borderColor = COL_PIN_BORDER,
        .cornerRadius = 4.0f,
    });

    auto *drag = node->addExtension<Amethyst::UIDragDetector>();
    drag->mode = Amethyst::DragMode::FREE;
    drag->onDragUpdate = [this, nodeId](Amethyst::vec2, Amethyst::vec2) { refreshNodeWires(nodeId); };

    node->track(node->onInputBeganCb.connect([this, nodeId](const Amethyst::InputObject &io) {
        if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
            selectNode(nodeId, false);
        } else if (io.type == Amethyst::InputType::MOUSE_BUTTON_2) {
            showNodeMenu(nodeId, Amethyst::vec2(io.position.x, io.position.y));
        }
    }));

    auto *header = node->add<Amethyst::Frame>();
    header->name = "Header";
    // Inset the header by the border so it sits inside the outline, centred on x via the anchor.
    header->setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.5f, 0.0f),
        .position = Amethyst::UDim2(0.5f, 0.0f, 0.0f, NODE_BORDER),
        .size = Amethyst::UDim2(1.0f, -2.0f * NODE_BORDER, 0.0f, NODE_HEADER_HEIGHT),
    });
    header->setBaseStyleProperties({.backgroundColor = headerColor, .cornerRadius = 4.0f});
    header->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

    auto *title = header->add<Amethyst::TextLabel>();
    title->setText(std::string(headerText));
    title->setBaseProperties({
        .padding = {.left = Amethyst::UDim::fromOffset(NODE_PADDING)},
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
    });
    title->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    title->setTextStyleProperties({.fontSize = 14.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER});
    title->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

    return node;
}

uint32_t NodeEditorPanel::spawnNode(Rapture::GraphNodeType type, std::string_view label, Amethyst::Color3 headerColor,
                                    Amethyst::vec2 canvasPos)
{
    if (m_content == nullptr) {
        return 0;
    }

    uint32_t nodeId = m_nextNodeId++;

    // A grouped node's header shows the group name; the variant is chosen with the node's dropdown.
    const NodeVariantGroup *group = s_variantGroupFor(type);
    std::string headerText = (group != nullptr) ? std::string(group->groupLabel) : std::string(label);
    auto *node = createNodeShell(nodeId, canvasPos, headerText, headerColor);

    NodeView view;
    view.frame = node;
    view.type = type;
    m_nodes[nodeId] = std::move(view);

    // Controls sit directly under the outputs; they and the pins share the plain row height.
    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(type);
    float controlRowY = NODE_HEADER_HEIGHT + static_cast<float>(def != nullptr ? def->outputs.size() : 0) * NODE_ROW_HEIGHT;
    layoutPins(nodeId);
    if (group != nullptr) {
        addVariantControl(nodeId, node, controlRowY);
    } else if (s_isConstantType(type)) {
        addConstantEditor(nodeId, node, controlRowY);
    }
    return nodeId;
}

// Builds the persistent generator backing a procedural texture node kind
static std::unique_ptr<Rapture::ProceduralTexture> s_createNoiseGenerator(NodeEditorPanel::TextureNodeKind kind)
{
    switch (kind) {
    case NodeEditorPanel::TextureNodeKind::WHITE_NOISE:
        return Rapture::ProceduralTexture::createWhiteNoiseGenerator();
    case NodeEditorPanel::TextureNodeKind::PERLIN_NOISE:
        return Rapture::ProceduralTexture::createPerlinNoiseGenerator();
    case NodeEditorPanel::TextureNodeKind::SIMPLEX_NOISE:
        return Rapture::ProceduralTexture::createSimplexNoiseGenerator();
    case NodeEditorPanel::TextureNodeKind::RIDGED_NOISE:
        return Rapture::ProceduralTexture::createRidgedNoiseGenerator();
    default:
        return nullptr;
    }
}

uint32_t NodeEditorPanel::spawnTextureNode(TextureNodeKind kind, std::string_view label, Amethyst::vec2 canvasPos)
{
    if (m_content == nullptr) {
        return 0;
    }

    uint32_t nodeId = m_nextNodeId++;

    auto *node = createNodeShell(nodeId, canvasPos, label, COL_CAT_TEXTURE);

    NodeView view;
    view.frame = node;
    view.textureData = std::make_unique<TextureNodeData>();
    view.textureData->kind = kind;
    if (kind != TextureNodeKind::ASSET) {
        view.textureData->generator = s_createNoiseGenerator(kind);
    }
    m_nodes[nodeId] = std::move(view);

    layoutTexturePins(nodeId);
    if (kind != TextureNodeKind::ASSET) {
        regenerateProcedural(nodeId);
    }
    return nodeId;
}

void NodeEditorPanel::layoutPins(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.frame == nullptr) {
        return;
    }
    NodeView &view = it->second;
    Amethyst::Frame *node = view.frame;

    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(view.type);
    size_t outCount = (def != nullptr) ? def->outputs.size() : 0;

    // A constant/variable edits its value through its control, so its input pin is not drawn.
    bool hideInputs = s_isConstantType(view.type);
    size_t inCount = (def != nullptr && !hideInputs) ? def->inputs.size() : 0;

    // A grouped node reserves one control row for its dropdown; a constant reserves one per component.
    size_t controlRows = 0;
    if (s_variantGroupFor(view.type) != nullptr) {
        controlRows = 1;
    } else if (s_isConstantType(view.type) && def != nullptr && !def->outputs.empty()) {
        controlRows = Rapture::graph_pinTypeComponents(def->outputs[0].type);
    }

    float rowCount = static_cast<float>(outCount + controlRows + inCount);
    float bodyHeight = rowCount * NODE_ROW_HEIGHT + NODE_PADDING;
    node->setBaseProperties({.size = Amethyst::UDim2::fromOffset(NODE_WIDTH, NODE_HEADER_HEIGHT + bodyHeight)});

    if (def == nullptr) {
        return;
    }

    // Outputs first (right, top down), then the control-row gap, then inputs (left).
    float rowY = NODE_HEADER_HEIGHT;
    uint32_t slot = 0;
    for (const auto &pin : def->outputs) {
        view.pinIds.push_back(addPin(nodeId, node, pin.name, pin.type, slot++, rowY, true));
        rowY += NODE_ROW_HEIGHT;
    }
    rowY += static_cast<float>(controlRows) * NODE_ROW_HEIGHT;
    if (hideInputs) {
        uint32_t valueSlot = 0;
        for (const auto &pin : def->inputs) {
            PinView pv;
            pv.nodeId = nodeId;
            pv.slotIndex = valueSlot;
            pv.type = pin.type;
            pv.value = std::make_unique<Rapture::PinValue>(pin.defaultValue);
            view.pinIds.push_back(m_pins.insert(std::move(pv)));
            ++valueSlot;
        }
        return;
    }
    slot = 0;
    for (const auto &pin : def->inputs) {
        uint32_t pinId = addPin(nodeId, node, pin.name, pin.type, slot, rowY, false);
        view.pinIds.push_back(pinId);

        PinView &pv = m_pins[pinId];
        pv.value = std::make_unique<Rapture::PinValue>(pin.defaultValue);

        if (Rapture::graph_pinTypeComponents(pin.type) == 1 && pin.type != Rapture::PinType::TEXTURE) {
            bool integer = (pin.type == Rapture::PinType::INT);
            float usable = NODE_WIDTH - 2.0f * NODE_PADDING;
            float x = NODE_PADDING + usable * 0.2f; // leave 20% on the left for the pin label
            float width = usable * 0.8f;
            pv.editor = addValueDrag(node, x, rowY, width, pv.value.get(), 0, integer);
            pv.editor->setBaseProperties({.visible = !isInputConnected(nodeId, slot)});
        }

        ++slot;
        rowY += NODE_ROW_HEIGHT;
    }

    if (view.type == Rapture::GraphNodeType::TEXTURE_SAMPLE) {
        if (view.textureData == nullptr) {
            view.textureData = std::make_unique<TextureNodeData>();
        }
        view.textureData->preview = addTexturePreview(node, rowY);
        node->setBaseProperties({.size = Amethyst::UDim2::fromOffset(NODE_WIDTH, rowY + TEXTURE_PREVIEW_HEIGHT + NODE_PADDING)});
    }
}

void NodeEditorPanel::addVariantControl(uint32_t nodeId, Amethyst::Frame *node, float rowY)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return;
    }
    const NodeVariantGroup *group = s_variantGroupFor(it->second.type);
    if (group == nullptr) {
        return;
    }

    auto *dropdown = node->add<Amethyst::Dropdown>();
    dropdown->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, NODE_PADDING, 0.0f, rowY + 1.0f),
        .size = Amethyst::UDim2(1.0f, -2.0f * NODE_PADDING, 0.0f, NODE_ROW_HEIGHT - 2.0f),
        .zIndex = 2,
    });

    std::vector<Amethyst::ContextMenuItem> items;
    for (const auto &variant : group->variants) {
        Rapture::GraphNodeType variantType = variant.type;
        std::string variantLabel = variant.label;
        items.push_back(Amethyst::ContextMenuItem::action(variantLabel, [this, nodeId, variantType, variantLabel, dropdown]() {
            dropdown->setText(variantLabel);
            changeNodeType(nodeId, variantType);
        }));
    }
    dropdown->setItems(std::move(items));
    dropdown->setText(std::string(s_variantLabel(*group, it->second.type)));

    NodeControl control;
    control.widgets.push_back(dropdown);
    it->second.controls.push_back(std::move(control));
}

void NodeEditorPanel::addConstantEditor(uint32_t nodeId, Amethyst::Frame *node, float rowY)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return;
    }
    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(it->second.type);
    if (def == nullptr || def->outputs.empty()) {
        return;
    }

    uint32_t valuePinId = findPin(nodeId, false, 0);
    if (valuePinId == INVALID_PIN || m_pins[valuePinId].value == nullptr) {
        return;
    }
    Rapture::PinValue *value = m_pins[valuePinId].value.get();

    Rapture::PinType type = def->outputs[0].type;
    bool integer = (type == Rapture::PinType::INT);
    uint32_t components = Rapture::graph_pinTypeComponents(type);

    NodeControl control;

    static constexpr const char *AXIS[4] = {"X", "Y", "Z", "W"};
    for (uint32_t c = 0; c < components; ++c) {
        float y = rowY + static_cast<float>(c) * NODE_ROW_HEIGHT;
        float dragX = NODE_PADDING;
        float dragWidth = NODE_WIDTH - 2.0f * NODE_PADDING;

        // A multi-component value labels each drag with its axis.
        if (components > 1) {
            static constexpr float AXIS_LABEL_WIDTH = 14.0f;
            auto *axisLabel = node->add<Amethyst::TextLabel>();
            axisLabel->setText(AXIS[c]);
            axisLabel->setBaseProperties({
                .position = Amethyst::UDim2::fromOffset(NODE_PADDING, y),
                .size = Amethyst::UDim2::fromOffset(AXIS_LABEL_WIDTH, NODE_ROW_HEIGHT),
            });
            axisLabel->setBaseStyleProperties({.backgroundTransparency = 1.0f});
            axisLabel->setTextStyleProperties({.fontSize = 12.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER});
            axisLabel->propagate(Amethyst::INTERACTION_CATEGORY_ALL);
            control.widgets.push_back(axisLabel);
            dragX += AXIS_LABEL_WIDTH;
            dragWidth -= AXIS_LABEL_WIDTH;
        }

        control.widgets.push_back(addValueDrag(node, dragX, y, dragWidth, value, c, integer));
    }

    it->second.controls.push_back(std::move(control));
}

Amethyst::UIObject *NodeEditorPanel::addValueDrag(Amethyst::Frame *node, float x, float y, float width, Rapture::PinValue *value,
                                                  uint32_t component, bool integer)
{
    auto *drag = node->add<Amethyst::DragFloat>();
    drag->valueF = &value->v4[component];
    drag->speed = integer ? 1.0 : 0.01;
    drag->setFormat(integer ? "%.0f" : "%.3f");
    drag->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(x, y + 1.0f),
        .size = Amethyst::UDim2::fromOffset(width, NODE_ROW_HEIGHT - 2.0f),
        .zIndex = 2,
    });
    return drag;
}

Amethyst::ImageLabel *NodeEditorPanel::addTexturePreview(Amethyst::Frame *node, float rowY)
{
    auto *preview = node->add<Amethyst::ImageLabel>();
    preview->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(NODE_PADDING, rowY),
        .size = Amethyst::UDim2::fromOffset(NODE_WIDTH - 2.0f * NODE_PADDING, TEXTURE_PREVIEW_HEIGHT),
    });
    preview->setBaseStyleProperties({.backgroundColor = COL_BG, .cornerRadius = 3.0f});
    return preview;
}

void NodeEditorPanel::layoutTexturePins(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.frame == nullptr || it->second.textureData == nullptr) {
        return;
    }
    NodeView &view = it->second;
    Amethyst::Frame *node = view.frame;

    float rowY = NODE_HEADER_HEIGHT;
    view.pinIds.push_back(addPin(nodeId, node, "Color", Rapture::PinType::VEC3, 0, rowY, true));
    rowY += NODE_ROW_HEIGHT;
    view.pinIds.push_back(addPin(nodeId, node, "Alpha", Rapture::PinType::FLOAT, 1, rowY, true));
    rowY += NODE_ROW_HEIGHT;

    view.textureData->preview = addTexturePreview(node, rowY);
    rowY += TEXTURE_PREVIEW_HEIGHT + NODE_PADDING;

    if (view.textureData->kind == TextureNodeKind::ASSET) {
        auto *picker = node->add<Amethyst::Dropdown>();
        picker->setBaseProperties({
            .position = Amethyst::UDim2(0.0f, NODE_PADDING, 0.0f, rowY + 1.0f),
            .size = Amethyst::UDim2(1.0f, -2.0f * NODE_PADDING, 0.0f, NODE_ROW_HEIGHT - 2.0f),
            .zIndex = 2,
        });
        picker->setText("Select texture");

        std::vector<Amethyst::ContextMenuItem> items;
        for (Rapture::AssetHandle handle : Rapture::AssetManager::getVirtualAssetsByType(Rapture::AssetType::TEXTURE)) {
            std::string name = Rapture::AssetManager::getAssetMetadata(handle).getName();
            items.push_back(Amethyst::ContextMenuItem::action(name, [this, nodeId, handle, picker, name]() {
                picker->setText(name);
                setTextureNodeAsset(nodeId, handle);
            }));
        }
        picker->setItems(std::move(items));

        NodeControl control;
        control.widgets.push_back(picker);
        view.controls.push_back(std::move(control));
        rowY += NODE_ROW_HEIGHT;
    } else if (view.textureData->generator != nullptr && view.textureData->generator->isValid()) {
        // One labelled drag per reflected generator parameter, seeded from its current value.
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
            addTextureParamRow(nodeId, node, rowY, desc, param.get());
            view.textureData->params.push_back(std::move(param));
            rowY += NODE_ROW_HEIGHT;
        }
    }

    // An unconnected UV falls back to the mesh texcoords when the node is lowered for compilation.
    view.pinIds.push_back(addPin(nodeId, node, "UV", Rapture::PinType::VEC2, 0, rowY, false));
    rowY += NODE_ROW_HEIGHT;

    node->setBaseProperties({.size = Amethyst::UDim2::fromOffset(NODE_WIDTH, rowY + NODE_PADDING)});
}

void NodeEditorPanel::setTextureNodeAsset(uint32_t nodeId, Rapture::AssetHandle handle)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.textureData == nullptr) {
        return;
    }

    Rapture::AssetRef assetRef = Rapture::AssetManager::getAsset(handle);
    if (!assetRef) {
        return;
    }
    auto *texture = assetRef.get()->getUnderlyingAsset<Rapture::Texture>();
    if (texture == nullptr) {
        return;
    }

    it->second.textureData->texture = Rapture::AssetPtr<Rapture::Texture>(assetRef);

    if (it->second.textureData->preview != nullptr && m_services.registerTexture) {
        it->second.textureData->preview->setImage(m_services.registerTexture(texture));
    }
}

void NodeEditorPanel::addTextureParamRow(uint32_t nodeId, Amethyst::Frame *node, float rowY,
                                         const Rapture::ProceduralParameter &desc, TextureParam *param)
{
    static constexpr float LABEL_FRACTION = 0.45f;
    float usable = NODE_WIDTH - 2.0f * NODE_PADDING;
    float labelWidth = usable * LABEL_FRACTION;

    auto *label = node->add<Amethyst::TextLabel>();
    label->setText(desc.displayName);
    label->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(NODE_PADDING, rowY),
        .size = Amethyst::UDim2::fromOffset(labelWidth, NODE_ROW_HEIGHT),
    });
    label->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    label->setTextStyleProperties({.fontSize = 12.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER});
    label->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

    float dragX = NODE_PADDING + labelWidth;
    float dragWidth = usable - labelWidth;
    Amethyst::BaseProperties dragProps = {
        .position = Amethyst::UDim2::fromOffset(dragX, rowY + 1.0f),
        .size = Amethyst::UDim2::fromOffset(dragWidth, NODE_ROW_HEIGHT - 2.0f),
        .zIndex = 2,
    };

    bool isInteger =
        desc.type == Rapture::PushConstantMemberInfo::BaseType::INT || desc.type == Rapture::PushConstantMemberInfo::BaseType::UINT;

    NodeControl control;
    if (isInteger) {
        auto *drag = node->add<Amethyst::DragInt>();
        drag->value = &param->i;
        drag->speed = 1;
        if (desc.hasRange) {
            drag->min = static_cast<int64_t>(desc.minValue);
            drag->max = static_cast<int64_t>(desc.maxValue);
        }
        drag->onValueChanged = [this, nodeId](int64_t) { regenerateProcedural(nodeId); };
        drag->setBaseProperties(dragProps);
        control.widgets.push_back(drag);
    } else {
        auto *drag = node->add<Amethyst::DragFloat>();
        drag->value = &param->f;
        drag->speed = desc.hasRange ? (desc.maxValue - desc.minValue) * 0.01 : 0.01;
        drag->setFormat("%.3f");
        if (desc.hasRange) {
            drag->min = desc.minValue;
            drag->max = desc.maxValue;
        }
        drag->onValueChanged = [this, nodeId](double) { regenerateProcedural(nodeId); };
        drag->setBaseProperties(dragProps);
        control.widgets.push_back(drag);
    }

    auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        control.widgets.push_back(label);
        it->second.controls.push_back(std::move(control));
    }
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

    // Push the edited drag values back into the generator, then re-run the compute pass.
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

    if (td.preview != nullptr && m_services.registerTexture) {
        td.preview->setImage(m_services.registerTexture(&td.generator->getTexture()));
    }
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

void NodeEditorPanel::activateCanvasLayer()
{
    m_content = m_canvas->add<Amethyst::Frame>();
    m_content->name = "Content";
    m_content->setBaseProperties({.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f)});
    m_content->setBaseStyleProperties({.backgroundTransparency = 1.0f});

    m_wireLayer = m_content->add<Amethyst::Frame>();
    m_wireLayer->name = "Wires";
    m_wireLayer->setBaseProperties({.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f)});
    m_wireLayer->setBaseStyleProperties({.backgroundTransparency = 1.0f});

    m_nodes.clear();
    m_pins = {};
    m_connections.clear();
    m_nextNodeId = 1;
    m_pan = Amethyst::vec2(0.0f);
}

void NodeEditorPanel::stashGraph(uint32_t graphId)
{
    GraphView view;
    view.content = m_content;
    view.wireLayer = m_wireLayer;
    view.nodes = std::move(m_nodes);
    view.pins = std::move(m_pins);
    view.connections = std::move(m_connections);
    view.nextNodeId = m_nextNodeId;
    view.pan = m_pan;

    auto props = m_content->getBaseProperties();
    props.visible = false;
    m_content->setBaseProperties(props);

    m_graphViews[graphId] = std::move(view);
}

void NodeEditorPanel::restoreGraph(GraphView &view)
{
    m_content = view.content;
    m_wireLayer = view.wireLayer;
    m_nodes = std::move(view.nodes);
    m_pins = std::move(view.pins);
    m_connections = std::move(view.connections);
    m_nextNodeId = view.nextNodeId;
    m_pan = view.pan;

    auto props = m_content->getBaseProperties();
    props.visible = true;
    m_content->setBaseProperties(props);
}

void NodeEditorPanel::connectPins(uint32_t srcNode, uint32_t srcSlot, uint32_t dstNode, uint32_t dstSlot)
{
    uint32_t outPin = findPin(srcNode, true, srcSlot);
    uint32_t inPin = findPin(dstNode, false, dstSlot);
    if (outPin == INVALID_PIN || inPin == INVALID_PIN) {
        return;
    }
    createWire(outPin, inPin);
}

void NodeEditorPanel::setConstantValue(uint32_t nodeId, const Rapture::PinValue &value)
{
    uint32_t pinId = findPin(nodeId, false, 0);
    if (pinId == INVALID_PIN || !m_pins.isLive(pinId) || m_pins[pinId].value == nullptr) {
        return;
    }
    *m_pins[pinId].value = value;
}

void NodeEditorPanel::setImageTexture(uint32_t nodeId, Rapture::AssetPtr<Rapture::Texture> texture)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.textureData == nullptr) {
        return;
    }
    Rapture::Texture *tex = texture.get();
    it->second.textureData->texture = std::move(texture);

    if (tex != nullptr && it->second.textureData->preview != nullptr && m_services.registerTexture) {
        it->second.textureData->preview->setImage(m_services.registerTexture(tex));
    }
}

bool NodeEditorPanel::isInputConnected(uint32_t nodeId, uint32_t slotIndex) const
{
    for (const auto &wire : m_connections) {
        if (m_pins.isLive(wire.dstPinId)) {
            const PinView &pin = m_pins[wire.dstPinId];
            if (pin.nodeId == nodeId && pin.slotIndex == slotIndex) {
                return true;
            }
        }
    }
    return false;
}

uint32_t NodeEditorPanel::addPin(uint32_t nodeId, Amethyst::Frame *node, std::string_view name, Rapture::PinType type,
                                 uint32_t slotIndex, float rowY, bool isOutput)
{
    auto *socket = node->add<Amethyst::Shape>(Amethyst::ShapeKind::CIRCLE);
    socket->setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
        .position = Amethyst::UDim2(isOutput ? 1.0f : 0.0f, 0.0f, 0.0f, rowY + NODE_ROW_HEIGHT * 0.5f),
        .size = Amethyst::UDim2::fromOffset(NODE_PIN_SIZE, NODE_PIN_SIZE),
        .zIndex = 2,
    });
    socket->setBaseStyleProperties({
        .backgroundColor = s_pinColor(type),
        .borderMode = Amethyst::BorderMode::OUTLINE,
        .borderPixelSize = NODE_PIN_BORDER,
        .borderColor = COL_PIN_BORDER,
    });

    auto *label = node->add<Amethyst::TextLabel>();
    label->setText(std::string(name));
    label->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, NODE_PADDING, 0.0f, rowY),
        .size = Amethyst::UDim2(1.0f, -2.0f * NODE_PADDING, 0.0f, NODE_ROW_HEIGHT),
    });
    label->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    label->setTextStyleProperties({
        .fontSize = 13.0f,
        .textXAlignment = isOutput ? Amethyst::TextXAlignment::RIGHT : Amethyst::TextXAlignment::LEFT,
        .textYAlignment = Amethyst::TextYAlignment::CENTER,
    });
    label->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

    PinView pin;
    pin.socket = socket;
    pin.label = label;
    pin.nodeId = nodeId;
    pin.slotIndex = slotIndex;
    pin.isOutput = isOutput;
    pin.type = type;
    pin.localOffset = Amethyst::vec2(isOutput ? NODE_WIDTH : 0.0f, rowY + NODE_ROW_HEIGHT * 0.5f);

    uint32_t pinId = m_pins.insert(std::move(pin));
    socket->track(socket->onInputBeganCb.connect([this, pinId](const Amethyst::InputObject &io) {
        if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
            beginConnection(pinId);
        }
    }));
    return pinId;
}

Amethyst::vec2 NodeEditorPanel::pinPosition(uint32_t pinId) const
{
    const PinView &pin = m_pins[pinId];
    auto it = m_nodes.find(pin.nodeId);
    if (it == m_nodes.end() || it->second.frame == nullptr) {
        return pin.localOffset;
    }
    return it->second.frame->getBaseProperties().position.offset + pin.localOffset;
}

uint32_t NodeEditorPanel::pinAt(Amethyst::vec2 contentPos) const
{
    uint32_t best = INVALID_PIN;
    float bestDist = PIN_HIT_RADIUS * PIN_HIT_RADIUS;
    m_pins.forEach([&](uint32_t id, const PinView &) {
        Amethyst::vec2 d = pinPosition(id) - contentPos;
        float dist = d.x * d.x + d.y * d.y;
        if (dist < bestDist) {
            bestDist = dist;
            best = id;
        }
    });
    return best;
}

uint32_t NodeEditorPanel::findPin(uint32_t nodeId, bool isOutput, uint32_t slotIndex) const
{
    uint32_t found = INVALID_PIN;
    m_pins.forEach([&](uint32_t id, const PinView &pin) {
        if (pin.nodeId == nodeId && pin.isOutput == isOutput && pin.slotIndex == slotIndex) {
            found = id;
        }
    });
    return found;
}

bool NodeEditorPanel::canConnect(uint32_t a, uint32_t b) const
{
    if (a == b || !m_pins.isLive(a) || !m_pins.isLive(b)) {
        return false;
    }
    if (m_pins[a].nodeId == m_pins[b].nodeId) {
        return false;
    }
    if (m_pins[a].type != m_pins[b].type) {
        return false;
    }
    return m_pins[a].isOutput != m_pins[b].isOutput;
}

void NodeEditorPanel::beginConnection(uint32_t pinId)
{
    if (m_canvas == nullptr || m_wireLayer == nullptr) {
        return;
    }

    m_connecting = true;
    m_connectSrcPinId = pinId;
    setHoverPin(INVALID_PIN);

    m_dragWire = m_wireLayer->add<Amethyst::Spline>();
    m_dragWire->setSplineProperties({
        .type = Amethyst::CurveType::LINEAR,
        .thickness = WIRE_THICKNESS,
        .color = Amethyst::Color4(s_pinColor(m_pins[pinId].type), 1.0f),
        .showKnots = false,
    });
    Amethyst::vec2 start = pinPosition(pinId);
    m_dragWire->setKnots({start, start});

    if (auto *window = m_canvas->getWindow()) {
        window->captureMouse(m_canvas);
    }
}

void NodeEditorPanel::updateConnectionDrag(Amethyst::vec2 cursorContent)
{
    if (m_dragWire != nullptr) {
        m_dragWire->setKnots({pinPosition(m_connectSrcPinId), cursorContent});
    }

    uint32_t target = pinAt(cursorContent);
    if (target != INVALID_PIN && !canConnect(m_connectSrcPinId, target)) {
        target = INVALID_PIN;
    }
    setHoverPin(target);
}

void NodeEditorPanel::finishConnection(Amethyst::vec2 cursorContent)
{
    uint32_t target = pinAt(cursorContent);
    setHoverPin(INVALID_PIN);

    if (target != INVALID_PIN && canConnect(m_connectSrcPinId, target)) {
        bool srcIsOutput = m_pins[m_connectSrcPinId].isOutput;
        uint32_t outPin = srcIsOutput ? m_connectSrcPinId : target;
        uint32_t inPin = srcIsOutput ? target : m_connectSrcPinId;
        createWire(outPin, inPin);
    }

    if (m_dragWire != nullptr && m_wireLayer != nullptr) {
        m_wireLayer->removeChild(m_dragWire);
    }
    m_dragWire = nullptr;
    m_connecting = false;

    if (auto *window = m_canvas->getWindow()) {
        window->releaseMouse(m_canvas);
    }
}

void NodeEditorPanel::createWire(uint32_t outPinId, uint32_t inPinId)
{
    if (m_wireLayer == nullptr) {
        return;
    }

    // An input holds at most one wire: drop any existing wire into that input.
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if (it->dstPinId == inPinId) {
            if (it->spline != nullptr) {
                m_wireLayer->removeChild(it->spline);
            }
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }

    auto *spline = m_wireLayer->add<Amethyst::Spline>();
    spline->setSplineProperties({
        .type = Amethyst::CurveType::LINEAR,
        .thickness = WIRE_THICKNESS,
        .color = Amethyst::Color4(s_pinColor(m_pins[outPinId].type), 1.0f),
        .showKnots = false,
    });

    WireView wire;
    wire.srcPinId = outPinId;
    wire.dstPinId = inPinId;
    wire.spline = spline;
    applyWireKnots(wire);
    m_connections.push_back(wire);

    // A wired input hides its inline default editor.
    if (m_pins.isLive(inPinId) && m_pins[inPinId].editor != nullptr) {
        m_pins[inPinId].editor->setBaseProperties({.visible = false});
    }
}

void NodeEditorPanel::refreshNodeWires(uint32_t nodeId)
{
    for (const auto &wire : m_connections) {
        if (m_pins[wire.srcPinId].nodeId == nodeId || m_pins[wire.dstPinId].nodeId == nodeId) {
            applyWireKnots(wire);
        }
    }
}

void NodeEditorPanel::applyWireKnots(const WireView &wire)
{
    if (wire.spline != nullptr) {
        wire.spline->setKnots({pinPosition(wire.srcPinId), pinPosition(wire.dstPinId)});
    }
}

void NodeEditorPanel::clearNodePins(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return;
    }
    NodeView &view = it->second;
    for (uint32_t pinId : view.pinIds) {
        if (!m_pins.isLive(pinId)) {
            continue;
        }
        PinView &pin = m_pins[pinId];
        if (view.frame != nullptr) {
            if (pin.socket != nullptr) {
                view.frame->removeChild(pin.socket);
            }
            if (pin.label != nullptr) {
                view.frame->removeChild(pin.label);
            }
            if (pin.editor != nullptr) {
                view.frame->removeChild(pin.editor);
            }
        }
        m_pins.remove(pinId);
    }
    view.pinIds.clear();
}

void NodeEditorPanel::changeNodeType(uint32_t nodeId, Rapture::GraphNodeType newType)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.type == newType) {
        return;
    }

    // Record where each wire touches this node (side, slot, type) so it can be re-pointed to the
    // rebuilt pins. A wire touches the node on exactly one side, so each appears here once.
    struct Touch {
        size_t wireIdx;
        bool isOutput;
        uint32_t slot;
        Rapture::PinType type;
    };
    std::vector<Touch> touches;
    for (size_t i = 0; i < m_connections.size(); ++i) {
        const WireView &wire = m_connections[i];
        if (m_pins.isLive(wire.srcPinId) && m_pins[wire.srcPinId].nodeId == nodeId) {
            const PinView &pin = m_pins[wire.srcPinId];
            touches.push_back({i, true, pin.slotIndex, pin.type});
        } else if (m_pins.isLive(wire.dstPinId) && m_pins[wire.dstPinId].nodeId == nodeId) {
            const PinView &pin = m_pins[wire.dstPinId];
            touches.push_back({i, false, pin.slotIndex, pin.type});
        }
    }

    it->second.type = newType;
    clearNodePins(nodeId);
    layoutPins(nodeId);

    // Re-point a wire when the same-side slot still exists with the same type; drop it otherwise.
    std::vector<size_t> dropped;
    for (const Touch &touch : touches) {
        uint32_t newPin = findPin(nodeId, touch.isOutput, touch.slot);
        if (newPin == INVALID_PIN || m_pins[newPin].type != touch.type) {
            dropped.push_back(touch.wireIdx);
            continue;
        }
        WireView &wire = m_connections[touch.wireIdx];
        if (touch.isOutput) {
            wire.srcPinId = newPin;
        } else {
            wire.dstPinId = newPin;
        }
    }

    // Erase dropped wires from the back so the lower indices stay valid as we go.
    std::sort(dropped.begin(), dropped.end(), std::greater<size_t>());
    for (size_t idx : dropped) {
        if (m_connections[idx].spline != nullptr && m_wireLayer != nullptr) {
            m_wireLayer->removeChild(m_connections[idx].spline);
        }
        m_connections.erase(m_connections.begin() + idx);
    }

    // layoutPins set editor visibility before the wires were re-pointed, so refresh it now that
    // m_connections is final.
    for (uint32_t pinId : it->second.pinIds) {
        if (!m_pins.isLive(pinId)) {
            continue;
        }
        PinView &pin = m_pins[pinId];
        if (!pin.isOutput && pin.editor != nullptr) {
            pin.editor->setBaseProperties({.visible = !isInputConnected(nodeId, pin.slotIndex)});
        }
    }

    refreshNodeWires(nodeId);
}

void NodeEditorPanel::setHoverPin(uint32_t pinId)
{
    if (pinId == m_hoverPinId) {
        return;
    }
    if (m_hoverPinId != INVALID_PIN && m_pins[m_hoverPinId].socket != nullptr) {
        m_pins[m_hoverPinId].socket->setBaseStyleProperties({.backgroundColor = s_pinColor(m_pins[m_hoverPinId].type)});
    }
    if (pinId != INVALID_PIN && m_pins[pinId].socket != nullptr) {
        m_pins[pinId].socket->setBaseStyleProperties({.backgroundColor = COL_PIN_HOVER});
    }
    m_hoverPinId = pinId;
}

void NodeEditorPanel::selectNode(uint32_t nodeId, bool additive)
{
    if (m_nodes.find(nodeId) == m_nodes.end()) {
        return;
    }
    if (!additive) {
        clearSelection();
    }

    uint32_t previousPrimary = m_primaryNodeId;
    m_selectedNodes.insert(nodeId);
    m_primaryNodeId = nodeId;

    // The old primary drops to the secondary style; the clicked node becomes primary.
    if (previousPrimary != 0 && previousPrimary != nodeId) {
        applyNodeBorder(previousPrimary);
    }
    applyNodeBorder(nodeId);
}

void NodeEditorPanel::clearSelection()
{
    std::unordered_set<uint32_t> previous = std::move(m_selectedNodes);
    m_selectedNodes.clear();
    m_primaryNodeId = 0;
    for (uint32_t nodeId : previous) {
        applyNodeBorder(nodeId);
    }
}

void NodeEditorPanel::applyNodeBorder(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end() || it->second.frame == nullptr) {
        return;
    }

    Amethyst::Color3 color = COL_PIN_BORDER;
    if (nodeId == m_primaryNodeId) {
        color = COL_NODE_SELECTED_PRIMARY;
    } else if (m_selectedNodes.count(nodeId) > 0) {
        color = COL_NODE_SELECTED;
    }
    it->second.frame->setBaseStyleProperties({.borderColor = color});
}

void NodeEditorPanel::showNodeMenu(uint32_t nodeId, Amethyst::vec2 screenPos)
{
    if (m_contextMenu == nullptr) {
        return;
    }

    // Right clicking a node outside the selection makes it the target so Delete acts on it.
    if (m_selectedNodes.count(nodeId) == 0) {
        selectNode(nodeId, false);
    }

    std::vector<Amethyst::ContextMenuItem> items;
    items.push_back(Amethyst::ContextMenuItem::action("Delete", [this]() { deleteSelection(); }));
    m_contextMenu->setItems(std::move(items));
    m_contextMenu->showAt(screenPos);
}

void NodeEditorPanel::deleteNode(uint32_t nodeId)
{
    auto it = m_nodes.find(nodeId);
    if (it == m_nodes.end()) {
        return;
    }

    // Drop every wire touching this node before its pins are freed. A wire whose far end is a
    // surviving node's input leaves that input unconnected, so its inline editor reappears.
    for (auto wireIt = m_connections.begin(); wireIt != m_connections.end();) {
        bool srcTouches = m_pins.isLive(wireIt->srcPinId) && m_pins[wireIt->srcPinId].nodeId == nodeId;
        bool dstTouches = m_pins.isLive(wireIt->dstPinId) && m_pins[wireIt->dstPinId].nodeId == nodeId;
        if (!srcTouches && !dstTouches) {
            ++wireIt;
            continue;
        }

        uint32_t dstPin = wireIt->dstPinId;
        if (!dstTouches && m_pins.isLive(dstPin) && m_pins[dstPin].editor != nullptr) {
            m_pins[dstPin].editor->setBaseProperties({.visible = true});
        }
        if (wireIt->spline != nullptr && m_wireLayer != nullptr) {
            m_wireLayer->removeChild(wireIt->spline);
        }
        wireIt = m_connections.erase(wireIt);
    }

    clearNodePins(nodeId);

    if (it->second.frame != nullptr && m_content != nullptr) {
        m_content->removeChild(it->second.frame);
    }

    m_nodes.erase(it);
    m_selectedNodes.erase(nodeId);
    if (m_primaryNodeId == nodeId) {
        m_primaryNodeId = 0;
    }
}

void NodeEditorPanel::deleteSelection()
{
    std::vector<uint32_t> targets(m_selectedNodes.begin(), m_selectedNodes.end());
    for (uint32_t nodeId : targets) {
        deleteNode(nodeId);
    }
}

Rapture::MaterialGraph NodeEditorPanel::buildGraph() const
{
    Rapture::MaterialGraph graph;

    uint32_t outputNodeId = 0;
    for (const auto &[nodeId, view] : m_nodes) {
        if (view.type == Rapture::GraphNodeType::SURFACE_OUTPUT) {
            outputNodeId = nodeId;
            break;
        }
    }
    if (outputNodeId == 0) {
        RP_WARN("Node editor has no Surface Output node, nothing to compile");
        return graph;
    }

    // Walk backwards from the output so only nodes feeding it are kept, leaving orphans out.
    std::unordered_set<uint32_t> reachable;
    std::vector<uint32_t> stack{outputNodeId};
    reachable.insert(outputNodeId);
    while (!stack.empty()) {
        uint32_t current = stack.back();
        stack.pop_back();
        for (const auto &wire : m_connections) {
            if (!m_pins.isLive(wire.srcPinId) || !m_pins.isLive(wire.dstPinId)) {
                continue;
            }
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

    // Synthesised primitives take fresh ids above every editor id, which m_nextNodeId already is.
    uint32_t nextSynthId = m_nextNodeId;

    auto inputWired = [&](uint32_t node, uint32_t slot) {
        for (const auto &wire : m_connections) {
            if (!m_pins.isLive(wire.dstPinId)) {
                continue;
            }
            const PinView &pin = m_pins[wire.dstPinId];
            if (pin.nodeId == node && !pin.isOutput && pin.slotIndex == slot) {
                return true;
            }
        }
        return false;
    };
    auto outputUsed = [&](uint32_t node, uint32_t slot) {
        for (const auto &wire : m_connections) {
            if (!m_pins.isLive(wire.srcPinId)) {
                continue;
            }
            const PinView &pin = m_pins[wire.srcPinId];
            if (pin.nodeId == node && pin.isOutput && pin.slotIndex == slot) {
                return true;
            }
        }
        return false;
    };

    for (uint32_t nodeId : reachable) {
        auto it = m_nodes.find(nodeId);
        if (it == m_nodes.end()) {
            continue;
        }
        const NodeView &view = it->second;

        if (view.textureData != nullptr && view.type == Rapture::GraphNodeType::NONE) {
            uint32_t sampleId = nextSynthId++;
            Rapture::GraphNode sample;
            sample.id = sampleId;
            sample.type = Rapture::GraphNodeType::TEXTURE_SAMPLE;
            sample.inputValues.resize(2);
            sample.inputTextures.resize(1);
            sample.inputTextures[0] = view.textureData->texture;
            graph.nodes.push_back(std::move(sample));

            // uv is the sample's second input; synth a TEXCOORD source when it is unconnected.
            inputRemap[pinKey(nodeId, 0)] = {sampleId, 1};
            if (!inputWired(nodeId, 0)) {
                uint32_t texcoordId = nextSynthId++;
                Rapture::GraphNode texcoord;
                texcoord.id = texcoordId;
                texcoord.type = Rapture::GraphNodeType::TEXCOORD;
                graph.nodes.push_back(std::move(texcoord));
                graph.connections.push_back({.srcNode = texcoordId, .srcPin = 0, .dstNode = sampleId, .dstPin = 1});
            }

            // Color reads the sample directly; the vec4 to vec3 coercion already takes rgb.
            outputRemap[pinKey(nodeId, 0)] = {sampleId, 0};

            // Alpha needs the w channel, so split the sample only when something consumes it.
            if (outputUsed(nodeId, 1)) {
                uint32_t splitId = nextSynthId++;
                Rapture::GraphNode split;
                split.id = splitId;
                split.type = Rapture::GraphNodeType::SPLIT_VEC4;
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

        // Carry every input pin's authored value across; the compiler ignores it on wired pins.
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

        if (view.type == Rapture::GraphNodeType::TEXTURE_SAMPLE && view.textureData != nullptr &&
            view.textureData->texture.get() != nullptr) {
            node.inputTextures.resize(1);
            node.inputTextures[0] = view.textureData->texture;
        }

        graph.nodes.push_back(std::move(node));
    }

    // A wire into a reachable node always has a reachable source, so keep every wire ending inside,
    // redirecting each endpoint that a lowered texture node moved onto a primitive pin.
    for (const auto &wire : m_connections) {
        if (!m_pins.isLive(wire.srcPinId) || !m_pins.isLive(wire.dstPinId)) {
            continue;
        }
        const PinView &src = m_pins[wire.srcPinId];
        const PinView &dst = m_pins[wire.dstPinId];
        if (reachable.count(dst.nodeId) == 0) {
            continue;
        }

        Endpoint from{src.nodeId, src.slotIndex};
        auto outIt = outputRemap.find(pinKey(src.nodeId, src.slotIndex));
        if (outIt != outputRemap.end()) {
            from = outIt->second;
        }
        Endpoint to{dst.nodeId, dst.slotIndex};
        auto inIt = inputRemap.find(pinKey(dst.nodeId, dst.slotIndex));
        if (inIt != inputRemap.end()) {
            to = inIt->second;
        }
        graph.connections.push_back({.srcNode = from.node, .srcPin = from.pin, .dstNode = to.node, .dstPin = to.pin});
    }

    return graph;
}

void NodeEditorPanel::compileGraph()
{
    if (m_selectedGraphId == UINT32_MAX) {
        RP_WARN("No material selected to compile into");
        return;
    }

    Rapture::MaterialGraph graph = buildGraph();
    if (graph.nodes.empty()) {
        return;
    }

    auto &graphs = Rapture::MaterialManager::getSurfaceGraphManager();
    if (!graphs.updateGraph(m_selectedGraphId, graph)) {
        RP_ERROR("Graph compile failed");
        return;
    }

    auto generatedDir = Rapture::Application::getInstance().getProject().getProjectShaderDirectory() / "glsl/generated";
    graphs.writeGeneratedFiles(generatedDir);
    graphs.notifyShadersOfRegeneration();
}

static Amethyst::ContextMenuItem s_categoryToMenuItem(const NodeCatalogCategory &category, const SpawnFn &spawn,
                                                      const TexSpawnFn &spawnTexture, Amethyst::Color3 color)
{
    std::vector<Amethyst::ContextMenuItem> items;

    for (const auto &entry : category.entries) {
        std::string label = entry.label;
        if (entry.textureKind != NodeEditorPanel::TextureNodeKind::NONE) {
            NodeEditorPanel::TextureNodeKind kind = entry.textureKind;
            items.push_back(Amethyst::ContextMenuItem::action(label, [spawnTexture, kind, label]() { spawnTexture(kind, label); }));
            continue;
        }
        Rapture::GraphNodeType type = entry.type;
        items.push_back(Amethyst::ContextMenuItem::action(label, [spawn, type, label, color]() { spawn(type, label, color); }));
    }

    for (const auto &sub : category.subcategories) {
        items.push_back(s_categoryToMenuItem(sub, spawn, spawnTexture, color));
    }

    return Amethyst::ContextMenuItem::submenu(category.label, std::move(items));
}
