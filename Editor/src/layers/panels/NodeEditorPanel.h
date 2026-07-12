#ifndef RAPTURE__NODE_EDITOR_PANEL_H
#define RAPTURE__NODE_EDITOR_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/shape.h>

#include "asset_manager/AssetCommon.h"
#include "events/ProjectEvents.h"
#include "layers/panels/Panel.h"
#include "materials/graph/MaterialGraph.h"
#include "materials/graph/MaterialGraphTypes.h"
#include "utils/FreeList.h"

namespace Rapture {
class ProceduralTexture;
struct ProceduralParameter;
class MaterialInstance;
}

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @brief Canvas panel that the material workspace uses to author node graphs
 *
 * Hosts a virtual canvas onto which nodes are placed. Right clicking the background opens a
 * categorised context menu that spawns a node at the cursor. Pins can be wired together by
 * dragging from one socket to another; wires are linear splines living in the content layer.
 */
class NodeEditorPanel : public Panel {
  public:
    NodeEditorPanel(Amethyst::TabBar *tabBar, const PanelServices &services);
    ~NodeEditorPanel() override;
    NodeEditorPanel(const NodeEditorPanel &) = delete;
    NodeEditorPanel &operator=(const NodeEditorPanel &) = delete;
    NodeEditorPanel(NodeEditorPanel &&) = delete;
    NodeEditorPanel &operator=(NodeEditorPanel &&) = delete;

    /**
     * @brief Builds the current graph, runs it through the compiler, and logs the outcome
     */
    void compileGraph(void);

    /**
     * @brief Which texture source an editor-only texture node samples
     */
    enum class TextureNodeKind {
        NONE,
        ASSET,
        WHITE_NOISE,
        PERLIN_NOISE,
        SIMPLEX_NOISE,
        RIDGED_NOISE,
    };

  private:
    static constexpr uint32_t INVALID_PIN = UINT32_MAX;
    static constexpr float TEXTURE_PREVIEW_HEIGHT = 120.0f;
    static constexpr float MATERIAL_BAR_HEIGHT = 34.0f;

    /**
     * @brief A single pin socket and the data needed to place and wire it
     */
    struct PinView {
        Amethyst::Shape *socket = nullptr;
        Amethyst::TextLabel *label = nullptr;
        uint32_t nodeId = 0;
        uint32_t slotIndex = 0; // index within the owning node's outputs or inputs
        bool isOutput = false;
        Rapture::PinType type = Rapture::PinType::FLOAT;
        Amethyst::vec2 localOffset{0.0f};         // pin centre relative to its node's origin
        std::unique_ptr<Rapture::PinValue> value; // an input pin's default, edited while it is unconnected
        Amethyst::UIObject *editor = nullptr;     // inline default editor, shown while the input is unconnected
    };

    /**
     * @brief A control-row widget group belonging to a node
     */
    struct NodeControl {
        std::vector<Amethyst::UIObject *> widgets;
    };

    /**
     * @brief Drag binding for one procedural parameter, indexing into the generator's parameter list
     */
    struct TextureParam {
        size_t index = 0;
        double f = 0.0;
        int64_t i = 0;
    };

    /**
     * @brief Editor-only state for a texture node, lowered to a texture sample at compile time
     */
    struct TextureNodeData {
        TextureNodeKind kind = TextureNodeKind::ASSET;
        Rapture::AssetPtr<Rapture::Texture> texture;
        Amethyst::ImageLabel *preview = nullptr;
        std::unique_ptr<Rapture::ProceduralTexture> generator;
        std::vector<std::unique_ptr<TextureParam>> params;
    };

    /**
     * @brief A spawned node's root frame, its graph type, and the pins it owns
     */
    struct NodeView {
        Amethyst::Frame *frame = nullptr;
        Rapture::GraphNodeType type = Rapture::GraphNodeType::NONE;
        std::vector<uint32_t> pinIds;
        std::vector<NodeControl> controls;
        std::unique_ptr<TextureNodeData> textureData;
    };

    /**
     * @brief A live connection: an output pin, an input pin, and the wire drawn between them
     */
    struct WireView {
        uint32_t srcPinId = 0;
        uint32_t dstPinId = 0;
        Amethyst::Spline *spline = nullptr;
    };

    void setupCanvas(void);
    void setupContextMenu(void);
    void setupMaterialBar(void);

    /**
     * @brief Repopulates the material dropdown from the loaded material assets
     */
    void rebuildMaterialList(void);

    /**
     * @brief Loads the picked material into the canvas as a graph
     * @param handle The material asset to load
     */
    void selectMaterial(Rapture::AssetHandle handle);

    /**
     * @brief Rebuilds the canvas from an authored graph, laying its nodes out left to right
     * @param graph The source graph to reconstruct as editor nodes and wires
     */
    void loadGraph(const Rapture::MaterialGraph &graph);

    /**
     * @brief Opens the add-node menu on right click, starts a pan on middle click
     * @param io The input that began on the canvas background
     */
    void onCanvasInputBegan(const Amethyst::InputObject &io);

    /**
     * @brief Advances an in-progress pan or connection drag by the current mouse position
     * @param io The move input routed to the canvas while the mouse is captured
     */
    void onCanvasInputChanged(const Amethyst::InputObject &io);

    /**
     * @brief Ends a pan or connection drag and releases the mouse capture
     * @param io The input that ended on the canvas background
     */
    void onCanvasInputEnded(const Amethyst::InputObject &io);

    /**
     * @brief Builds the categorised add-node submenu tree from the editor node catalog
     * @return Context menu items for the canvas background menu
     */
    std::vector<Amethyst::ContextMenuItem> buildAddMenu(void);

    /**
     * @brief Builds the shared node frame, drag handling, input hooks, and header
     * @param nodeId The node's id
     * @param canvasPos The node's origin in content space
     * @param headerText The header label text
     * @param headerColor The header background colour
     * @return The created node frame
     */
    Amethyst::Frame *createNodeShell(uint32_t nodeId, Amethyst::vec2 canvasPos, std::string_view headerText,
                                     Amethyst::Color3 headerColor);

    /**
     * @brief Spawns a node frame at a content-space position
     * @param type The graph node type the visual represents
     * @param label The node header text
     * @param headerColor The node header colour, taken from its menu category
     * @param canvasPos The node's origin in content space
     * @return The new node's id
     */
    uint32_t spawnNode(Rapture::GraphNodeType type, std::string_view label, Amethyst::Color3 headerColor, Amethyst::vec2 canvasPos);

    /**
     * @brief Spawns an editor-only texture node at a content-space position
     * @param kind The texture source the node samples
     * @param label The node header text
     * @param canvasPos The node's origin in content space
     * @return The new node's id
     */
    uint32_t spawnTextureNode(TextureNodeKind kind, std::string_view label, Amethyst::vec2 canvasPos);

    /**
     * @brief Lays out a texture node's pins, preview image, and source controls
     * @param nodeId The texture node to populate
     */
    void layoutTexturePins(uint32_t nodeId);

    /**
     * @brief Adds a texture preview image to a node at a row offset
     * @param node The node frame the preview is parented to
     * @param rowY The preview's top offset within the node
     * @return The created preview image
     */
    Amethyst::ImageLabel *addTexturePreview(Amethyst::Frame *node, float rowY);

    /**
     * @brief Points a texture asset node at a loaded texture and refreshes its preview
     * @param nodeId The texture node to assign
     * @param handle The texture asset to sample
     */
    void setTextureNodeAsset(uint32_t nodeId, Rapture::AssetHandle handle);

    /**
     * @brief Adds one reflected generator parameter to a procedural node as a labelled drag
     * @param nodeId The owning texture node's id
     * @param node The node frame the row is parented to
     * @param rowY The row's top offset within the node
     * @param desc The reflected parameter the drag edits
     * @param param The drag binding backing the parameter
     */
    void addTextureParamRow(uint32_t nodeId, Amethyst::Frame *node, float rowY, const Rapture::ProceduralParameter &desc,
                            TextureParam *param);

    /**
     * @brief Regenerates a procedural texture node's image from its current parameters
     * @param nodeId The procedural texture node to regenerate
     */
    void regenerateProcedural(uint32_t nodeId);

    /**
     * @brief Deletes every node, pin, and wire, resetting the canvas to empty
     */
    void clearGraph(void);

    /**
     * @brief Wires an output pin of one node to an input pin of another
     * @param srcNode The source node id
     * @param srcSlot The source node's output pin index
     * @param dstNode The destination node id
     * @param dstSlot The destination node's input pin index
     */
    void connectPins(uint32_t srcNode, uint32_t srcSlot, uint32_t dstNode, uint32_t dstSlot);

    /**
     * @brief Sets a constant node's authored value
     * @param nodeId The constant node id
     * @param value The value to store
     */
    void setConstantValue(uint32_t nodeId, const Rapture::PinValue &value);

    /**
     * @brief Binds an image node to a texture and refreshes its preview
     * @param nodeId The image node id
     * @param texture The texture the node samples
     */
    void setImageTexture(uint32_t nodeId, Rapture::AssetPtr<Rapture::Texture> texture);

    /**
     * @brief Lays out a node's pins for its current type
     * @param nodeId The node to populate
     */
    void layoutPins(uint32_t nodeId);

    /**
     * @brief Switches a node to a different typed variant in place
     * @param nodeId The node to retype
     * @param newType The variant to switch to
     */
    void changeNodeType(uint32_t nodeId, Rapture::GraphNodeType newType);

    /**
     * @brief Adds one pin row (edge socket + label) to a node and registers it
     * @param nodeId The owning node's id
     * @param node The node frame the pin is parented to
     * @param name The pin label text
     * @param type The pin data type, drives the socket colour
     * @param slotIndex The pin's index within its direction group (outputs or inputs)
     * @param rowY The row's top offset within the node
     * @param isOutput True for an output (socket right), false for an input (socket left)
     * @return The new pin's id
     */
    uint32_t addPin(uint32_t nodeId, Amethyst::Frame *node, std::string_view name, Rapture::PinType type, uint32_t slotIndex,
                    float rowY, bool isOutput);

    /**
     * @brief Adds the variant-selector dropdown control row to a node
     * @param nodeId The owning node's id
     * @param node The node frame the control is parented to
     * @param rowY The row's top offset within the node
     */
    void addVariantControl(uint32_t nodeId, Amethyst::Frame *node, float rowY);

    /**
     * @brief Adds a constant node's value editor as a control, one drag per component
     * @param nodeId The owning node's id
     * @param node The node frame the control is parented to
     * @param rowY The control's top offset within the node
     */
    void addConstantEditor(uint32_t nodeId, Amethyst::Frame *node, float rowY);

    /**
     * @brief Adds one number drag bound to a component of a value
     * @param node The node frame the drag is parented to
     * @param x The drag's left offset within the node
     * @param y The drag's top offset within the node
     * @param width The drag's width
     * @param value The value the drag edits
     * @param component The component index into the value
     * @param integer True to edit as a whole number
     * @return The created drag
     */
    Amethyst::UIObject *addValueDrag(Amethyst::Frame *node, float x, float y, float width, Rapture::PinValue *value,
                                     uint32_t component, bool integer);

    /**
     * @brief Whether an input pin currently has a wire into it
     * @param nodeId The owning node's id
     * @param slotIndex The input pin's index
     * @return True if a wire ends on that input
     */
    bool isInputConnected(uint32_t nodeId, uint32_t slotIndex) const;

    /**
     * @brief The pin socket's centre in content (graph) space
     * @param pinId The pin id
     * @return The pin centre, in content local coordinates
     */
    Amethyst::vec2 pinPosition(uint32_t pinId) const;

    /**
     * @brief Finds the pin whose socket is nearest a content space point, within a hit radius
     * @param contentPos The point in content local coordinates
     * @return The nearest pin id, or INVALID_PIN if none is close enough
     */
    uint32_t pinAt(Amethyst::vec2 contentPos) const;

    /**
     * @brief Finds a node's pin by direction and slot index
     * @param nodeId The owning node's id
     * @param isOutput True to match an output pin, false for an input
     * @param slotIndex The pin's index within that direction group
     * @return The pin id, or INVALID_PIN if the node has no such pin
     */
    uint32_t findPin(uint32_t nodeId, bool isOutput, uint32_t slotIndex) const;

    /**
     * @brief Whether two pins may be connected
     * @param a First pin id
     * @param b Second pin id
     * @return True if a valid connection could be made between them
     */
    bool canConnect(uint32_t a, uint32_t b) const;

    void beginConnection(uint32_t pinId);
    void updateConnectionDrag(Amethyst::vec2 cursorContent);
    void finishConnection(Amethyst::vec2 cursorContent);

    /**
     * @brief Creates a wire between an output and an input, replacing the input's existing wire
     * @param outPinId The output pin id
     * @param inPinId The input pin id
     */
    void createWire(uint32_t outPinId, uint32_t inPinId);

    /**
     * @brief Re-points every wire touching a node (called when the node moves)
     * @param nodeId The node whose wires to refresh
     */
    void refreshNodeWires(uint32_t nodeId);

    void applyWireKnots(const WireView &wire);

    /**
     * @brief Destroys a node's pin widgets and frees their slots
     * @param nodeId The node whose pins to tear down
     */
    void clearNodePins(uint32_t nodeId);

    /**
     * @brief Sets which pin is highlighted as the connection drop target
     * @param pinId The pin to highlight, or INVALID_PIN to clear the highlight
     */
    void setHoverPin(uint32_t pinId);

    /**
     * @brief Makes a node the primary selection, optionally keeping the current selection
     * @param nodeId The node to select
     * @param additive True to add to the selection, false to replace it
     */
    void selectNode(uint32_t nodeId, bool additive);

    /**
     * @brief Clears the selection and restores every selected node's default border
     */
    void clearSelection(void);

    /**
     * @brief Applies a node's border colour for its current selection state
     * @param nodeId The node to restyle
     */
    void applyNodeBorder(uint32_t nodeId);

    /**
     * @brief Opens the per-node context menu, selecting the node first if it is not already
     * @param nodeId The node the menu acts on
     * @param screenPos The screen position to open the menu at
     */
    void showNodeMenu(uint32_t nodeId, Amethyst::vec2 screenPos);

    /**
     * @brief Deletes a node, its pins, and every wire touching it
     * @param nodeId The node to delete
     */
    void deleteNode(uint32_t nodeId);

    /**
     * @brief Deletes every currently selected node
     */
    void deleteSelection(void);

    /**
     * @brief Extracts a compiler-ready graph from the current editor state
     * @return The built graph, or an empty graph if there is no surface output node
     */
    Rapture::MaterialGraph buildGraph(void) const;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::Frame *m_materialBar = nullptr;
    Amethyst::Dropdown *m_materialDropdown = nullptr;
    Amethyst::Frame *m_canvas = nullptr;
    Amethyst::Frame *m_content = nullptr;
    Amethyst::Frame *m_wireLayer = nullptr;
    Amethyst::ContextMenu *m_contextMenu = nullptr;

    Amethyst::EventConnection m_canvasBeganConn;
    Amethyst::EventConnection m_canvasMovedConn;
    Amethyst::EventConnection m_canvasEndedConn;

    Amethyst::vec2 m_menuScreenPos{0.0f};

    bool m_panning = false;
    Amethyst::vec2 m_pan{0.0f};
    Amethyst::vec2 m_panLastMouse{0.0f};

    std::unordered_map<uint32_t, NodeView> m_nodes;
    Rapture::FreeList<PinView> m_pins;
    std::vector<WireView> m_connections;

    bool m_connecting = false;
    uint32_t m_connectSrcPinId = 0;
    uint32_t m_hoverPinId = INVALID_PIN;
    Amethyst::Spline *m_dragWire = nullptr;

    std::unordered_set<uint32_t> m_selectedNodes;
    uint32_t m_primaryNodeId = 0; // the main selected node, 0 when nothing is selected

    uint32_t m_nextNodeId = 1;

    Rapture::EventListenerId m_serializeListener = 0;
};

#endif // RAPTURE__NODE_EDITOR_PANEL_H
