#include "NodeEditorPanel.h"

#include "Icons.h"
#include "layers/panels/components/tab_layouts.h"

#include "materials/graph/NodeRegistry.h"

#include <components/extensions/ui_drag_detector.h>
#include <components/shape.h>
#include <components/text_label.h>

#include <functional>
#include <memory>
#include <string>

#define COL_NODE_BODY  Amethyst::Color3::fromHex(0x21262e)
#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

#define COL_CAT_INPUT     Amethyst::Color3::fromHex(0x3a6ea5)
#define COL_CAT_UTILITIES Amethyst::Color3::fromHex(0x555b66)
#define COL_CAT_GEOMETRY  Amethyst::Color3::fromHex(0xa5613a)
#define COL_CAT_COLOR     Amethyst::Color3::fromHex(0xa59a3a)
#define COL_CAT_OUTPUT    Amethyst::Color3::fromHex(0x3a8a4f)
#define COL_CAT_DEFAULT   Amethyst::Color3::fromHex(0x394150)

#define COL_PIN_FLOAT Amethyst::Color3::fromHex(0xa1a1a1)
#define COL_PIN_INT   Amethyst::Color3::fromHex(0x4f9d55)
#define COL_PIN_VEC2  Amethyst::Color3::fromHex(0x5fb0c9)
#define COL_PIN_VEC3  Amethyst::Color3::fromHex(0x6b6bd6)
#define COL_PIN_VEC4  Amethyst::Color3::fromHex(0xd0b24a)
#define COL_PIN_HOVER  Amethyst::Color3::fromHex(0xffffff)
#define COL_PIN_BORDER Amethyst::Color3::fromHex(0x1a1d21)

static constexpr float NODE_WIDTH = 168.0f;
static constexpr float NODE_HEADER_HEIGHT = 26.0f;
static constexpr float NODE_ROW_HEIGHT = 22.0f;
static constexpr float NODE_PADDING = 8.0f;
static constexpr float NODE_PIN_SIZE = 12.0f;
static constexpr float NODE_PIN_BORDER = 1.5f;
static constexpr float NODE_BORDER = 2.0f;
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
    m_canvas->setBaseStyleProperties({.backgroundTransparency = 1.0f});

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

    // Pins come straight from the node definition: outputs first (right, top down), then inputs
    // (left, continuing on the next rows).
    const Rapture::NodeDefinition *def = Rapture::NodeRegistry::get(type);
    size_t rowCount = (def != nullptr) ? def->outputs.size() + def->inputs.size() : 0;
    float bodyHeight = static_cast<float>(rowCount) * NODE_ROW_HEIGHT + NODE_PADDING;

    // Convert the screen space right click into content (graph) space so the node lands under the
    // cursor regardless of the current pan.
    Amethyst::vec2 canvasPos = m_menuScreenPos - m_content->absolutePosition;

    uint32_t nodeId = m_nextNodeId++;

    auto *node = m_content->add<Amethyst::Frame>();
    node->name = "Node " + std::to_string(nodeId);
    // Do not clip: pin sockets straddle the node border.
    node->setBaseProperties({
        .clipsDescendants = false,
        .position = Amethyst::UDim2::fromOffset(canvasPos.x, canvasPos.y),
        .size = Amethyst::UDim2::fromOffset(NODE_WIDTH, NODE_HEADER_HEIGHT + bodyHeight),
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
    title->setText(std::string(label));
    title->setBaseProperties({
        .padding = {.left = Amethyst::UDim::fromOffset(NODE_PADDING)},
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
    });
    title->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    title->setTextStyleProperties({.fontSize = 14.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER});
    title->propagate(Amethyst::INTERACTION_CATEGORY_ALL);

    NodeView view;
    view.frame = node;

    if (def != nullptr) {
        float rowY = NODE_HEADER_HEIGHT;
        for (const auto &pin : def->outputs) {
            view.pinIds.push_back(addPin(nodeId, node, pin.name, pin.type, rowY, true));
            rowY += NODE_ROW_HEIGHT;
        }
        for (const auto &pin : def->inputs) {
            view.pinIds.push_back(addPin(nodeId, node, pin.name, pin.type, rowY, false));
            rowY += NODE_ROW_HEIGHT;
        }
    }

    m_nodes[nodeId] = std::move(view);
}

uint32_t NodeEditorPanel::addPin(uint32_t nodeId, Amethyst::Frame *node, std::string_view name, Rapture::PinType type,
                                 float rowY, bool isOutput)
{
    uint32_t pinId = static_cast<uint32_t>(m_pins.size());

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
    pin.nodeId = nodeId;
    pin.isOutput = isOutput;
    pin.type = type;
    pin.localOffset = Amethyst::vec2(isOutput ? NODE_WIDTH : 0.0f, rowY + NODE_ROW_HEIGHT * 0.5f);
    pin.pressConn = socket->onInputBeganCb.connect([this, pinId](const Amethyst::InputObject &io) {
        if (io.type == Amethyst::InputType::MOUSE_BUTTON_1) {
            beginConnection(pinId);
        }
    });
    m_pins.push_back(std::move(pin));
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
    for (uint32_t i = 0; i < m_pins.size(); ++i) {
        Amethyst::vec2 d = pinPosition(i) - contentPos;
        float dist = d.x * d.x + d.y * d.y;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

bool NodeEditorPanel::canConnect(uint32_t a, uint32_t b) const
{
    if (a == b || a >= m_pins.size() || b >= m_pins.size()) {
        return false;
    }
    if (m_pins[a].nodeId == m_pins[b].nodeId) {
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
