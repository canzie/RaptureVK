#include "NodeEditorPanel.h"

#include "Icons.h"
#include "layers/panels/components/tab_layouts.h"

#include "materials/graph/NodeRegistry.h"

#include <components/drag.h>
#include <components/dropdown.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/shape.h>
#include <components/text_label.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#define COL_NODE_BODY  Amethyst::Color3::fromHex(0x303030)
#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

#define COL_BG Amethyst::Color3::fromHex(0x1a1a1a)

#define COL_CAT_INPUT     Amethyst::Color3::fromHex(0x3a6ea5)
#define COL_CAT_UTILITIES Amethyst::Color3::fromHex(0x555b66)
#define COL_CAT_GEOMETRY  Amethyst::Color3::fromHex(0xa5613a)
#define COL_CAT_COLOR     Amethyst::Color3::fromHex(0xa59a3a)
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
        {"Output", {{"Surface Output", GNT::SURFACE_OUTPUT}}, {}},
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

static Amethyst::ContextMenuItem s_categoryToMenuItem(const NodeCatalogCategory &category, const SpawnFn &spawn,
                                                      Amethyst::Color3 color);

NodeEditorPanel::NodeEditorPanel(Amethyst::TabBar *tabBar, const PanelServices &services) : Panel(services)
{
    Rapture::NodeRegistry::registerBuiltins();

    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_canvas = nullptr;
        m_content = nullptr;
        m_wireLayer = nullptr;
        m_contextMenu = nullptr;
        m_dragWire = nullptr;
        m_connecting = false;
        m_selectedNodes.clear();
        m_primaryNodeId = 0;
    });
    m_root->name = "Node Editor";
    m_root->addClass("background-secondary");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupCanvas();
    setupContextMenu();

    tabBar->addTab(std::move(root), iconTabLayout("Node Editor", Icons::SVG_MATERIAL));
}

NodeEditorPanel::~NodeEditorPanel()
{
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
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
    });
    m_canvas->setBaseStyleProperties({.backgroundColor = COL_BG, .backgroundTransparency = 0.0f});

    // The content layer is a zero size, non clipping transform anchor: nodes live in its local
    // (graph) space, so panning is a single write to its offset and every node follows for free.
    m_content = m_canvas->add<Amethyst::Frame>();
    m_content->name = "Content";
    m_content->setBaseProperties({.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f)});
    m_content->setBaseStyleProperties({.backgroundTransparency = 1.0f});

    // Wires render under the nodes (nodes use zIndex 1), in the same content space.
    m_wireLayer = m_content->add<Amethyst::Frame>();
    m_wireLayer->name = "Wires";
    m_wireLayer->setBaseProperties({.clipsDescendants = false, .size = Amethyst::UDim2::fromOffset(0.0f, 0.0f)});
    m_wireLayer->setBaseStyleProperties({.backgroundTransparency = 1.0f});

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

std::vector<Amethyst::ContextMenuItem> NodeEditorPanel::buildAddMenu()
{
    SpawnFn spawn = [this](Rapture::GraphNodeType type, std::string_view label, Amethyst::Color3 color) {
        spawnNode(type, label, color);
    };

    std::vector<Amethyst::ContextMenuItem> addItems;
    for (const auto &category : s_nodeCatalog()) {
        addItems.push_back(s_categoryToMenuItem(category, spawn, s_categoryColor(category.label)));
    }

    std::vector<Amethyst::ContextMenuItem> root;
    root.push_back(Amethyst::ContextMenuItem::submenu("Add", std::move(addItems)));
    root.push_back(Amethyst::ContextMenuItem::action("Paste", [] {}).withEnabled(false));
    return root;
}

void NodeEditorPanel::spawnNode(Rapture::GraphNodeType type, std::string_view label, Amethyst::Color3 headerColor)
{
    if (m_content == nullptr) {
        return;
    }

    // Convert the screen space right click into content (graph) space so the node lands under the
    // cursor regardless of the current pan.
    Amethyst::vec2 canvasPos = m_menuScreenPos - m_content->absolutePosition;

    uint32_t nodeId = m_nextNodeId++;

    auto *node = m_content->add<Amethyst::Frame>();
    node->name = "Node " + std::to_string(nodeId);
    // Do not clip: pin sockets straddle the node border. The size is set by layoutPins.
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

    // A grouped node's header shows the group name; the variant is chosen with the node's dropdown.
    const NodeVariantGroup *group = s_variantGroupFor(type);
    std::string headerText = (group != nullptr) ? std::string(group->groupLabel) : std::string(label);

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
    title->setText(headerText);
    title->setBaseProperties({
        .padding = {.left = Amethyst::UDim::fromOffset(NODE_PADDING)},
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
    });
    title->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    title->setTextStyleProperties({.fontSize = 14.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER});
    title->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

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

        // A scalar input carries an inline drag on its own row, hidden while the input is wired.
        if (Rapture::graph_pinTypeComponents(pin.type) == 1) {
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
    auto *socket = node->add<Amethyst::Shape>(Amethyst::PRIMITIVE_CIRCLE);
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

static Amethyst::ContextMenuItem s_categoryToMenuItem(const NodeCatalogCategory &category, const SpawnFn &spawn,
                                                      Amethyst::Color3 color)
{
    std::vector<Amethyst::ContextMenuItem> items;

    for (const auto &entry : category.entries) {
        Rapture::GraphNodeType type = entry.type;
        std::string label = entry.label;
        items.push_back(Amethyst::ContextMenuItem::action(label, [spawn, type, label, color]() { spawn(type, label, color); }));
    }

    for (const auto &sub : category.subcategories) {
        items.push_back(s_categoryToMenuItem(sub, spawn, color));
    }

    return Amethyst::ContextMenuItem::submenu(category.label, std::move(items));
}
