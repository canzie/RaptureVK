#ifndef RAPTURE__NODE_EDITOR_PANEL_H
#define RAPTURE__NODE_EDITOR_PANEL_H

#include "panels/Panel.h"
#include "ui/GraphVisuals.h"

#include <amethyst/Amethyst.h>
#include <components/shape.h>
#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/assets/materials/graph/MaterialGraph.h>
#include <rapture/assets/materials/graph/MaterialGraphTypes.h>
#include <rapture/assets/textures/ATexture.h>
#include <rapture/core/events/EventSignal.h>
#include <rapture/core/utils/FreeList.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Rapture {
class ProceduralTexture;
struct ProceduralParameter;
} // namespace Rapture

class SearchField;

/**
 * @brief The material workspace's surface graph: nodes on a dotted canvas, wired pin to pin
 *
 * Right clicking the canvas opens a searchable list of the nodes that can be added there. Dragging from
 * a pin to another pin of the same type wires them, and an input holds at most one wire.
 */
class NodeEditorPanel : public Panel {
  public:
    std::string_view typeName() const override { return "NodeEditorPanel"; }
    NodeEditorPanel(const WorkspaceContext &context, Rapture::AssetHandle material);
    ~NodeEditorPanel() override;

    /**
     * @brief Builds the current graph and compiles it into the material's shader
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

    /**
     * @brief One editable value row of a node: an unconnected input or a constant's value
     */
    struct NodeValueRow {
        std::string label;
        std::span<float> values;
        bool wholeNumbers = false;
        bool connected = false;
    };

    /**
     * @brief Fired when a material is opened, carrying its asset handle
     * @return The signal to connect a preview to
     */
    Rapture::EventSignal<void(Rapture::AssetHandle)> &onMaterialSelectionChanged() { return m_onMaterialSelectionChanged; }

    /**
     * @brief Fired when the node shown as the selection changes, carrying its id or 0 for none
     * @return The signal
     */
    Rapture::EventSignal<void(uint32_t)> &onPrimaryNodeChanged() { return m_onPrimaryNodeChanged; }

    /**
     * @brief Fired when a node is added or removed, or its pins, wires or texture change, carrying the node's id
     * @return The signal
     */
    Rapture::EventSignal<void(uint32_t)> &onNodeEdited() { return m_onNodeEdited; }

    /**
     * @brief Fired when a value is edited in a node's own fields, carrying the node's id
     * @return The signal
     */
    Rapture::EventSignal<void(uint32_t)> &onNodeValuesChanged() { return m_onNodeValuesChanged; }

    /**
     * @brief Fired after a compile, carrying whether it succeeded and how many nodes the graph holds
     * @return The signal
     */
    Rapture::EventSignal<void(bool, size_t)> &onCompiled() { return m_onCompiled; }

    /**
     * @brief The node shown as the selection
     * @return Its id, or 0 when nothing is selected
     */
    uint32_t getPrimaryNode(void) const { return m_primaryNodeId; }

    /**
     * @brief How many nodes the graph holds
     * @return The node count
     */
    size_t getNodeCount(void) const { return m_nodes.size(); }

    /**
     * @brief The text in a node's header
     * @param nodeId The node
     * @return The node's title
     */
    std::string_view getNodeTitle(uint32_t nodeId) const;

    /**
     * @brief The category a node belongs to
     * @param nodeId The node
     * @return The node's category
     */
    GraphCategory getNodeCategory(uint32_t nodeId) const;

    /**
     * @brief The typed variants a node can switch between
     * @param nodeId The node
     * @return The variant labels, empty when the node has no variants
     */
    std::vector<std::string> getNodeVariantLabels(uint32_t nodeId) const;

    /**
     * @brief The label of the variant a node currently is
     * @param nodeId The node
     * @return The variant label, empty when the node has no variants
     */
    std::string_view getNodeVariantLabel(uint32_t nodeId) const;

    /**
     * @brief Switches a node to one of its typed variants
     * @param nodeId The node
     * @param variantIndex Index into the node's variant labels
     */
    void setNodeVariant(uint32_t nodeId, size_t variantIndex);

    /**
     * @brief Whether a node samples a texture asset that can be picked
     * @param nodeId The node
     * @return True for a node sampling a texture asset
     */
    bool samplesTextureAsset(uint32_t nodeId) const;

    /**
     * @brief The texture asset a node samples
     * @param nodeId The node
     * @return The texture's handle, or an invalid handle when it samples none
     */
    Rapture::AssetHandle getNodeTexture(uint32_t nodeId) const;

    /**
     * @brief Points a texture sampling node at a texture asset
     * @param nodeId The node
     * @param handle The texture asset to sample
     */
    void setNodeTexture(uint32_t nodeId, Rapture::AssetHandle handle);

    /**
     * @brief The editable values a node holds, in the order its rows show them
     * @param nodeId The node
     * @return One row per input with a value, or the value of a constant
     */
    std::vector<NodeValueRow> getNodeValueRows(uint32_t nodeId);

    /**
     * @brief Shows values written from outside the graph in the node's own fields
     * @param nodeId The node whose values changed
     */
    void refreshNodeFields(uint32_t nodeId);

  protected:
    void build(Amethyst::UIScope &scope) override;
    void onRootDestroyed() override;

  private:
    static constexpr uint32_t INVALID_PIN = UINT32_MAX;

    /**
     * @brief A pin's socket and what it carries
     */
    struct PinView {
        Amethyst::Shape *socket = nullptr;
        uint32_t nodeId = 0;
        uint32_t slotIndex = 0;
        bool isOutput = false;
        Rapture::PinType type = Rapture::PinType::FLOAT;
        std::string name;
        std::unique_ptr<Rapture::PinValue> value;
        Amethyst::UIObject *inlineField = nullptr;
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
        Rapture::Ref<Rapture::ATexture> texture;
        Amethyst::ImageLabel *preview = nullptr;
        std::unique_ptr<Rapture::ProceduralTexture> generator;
        std::vector<std::unique_ptr<TextureParam>> params;
    };

    /**
     * @brief A node on the canvas: its frame, the body its rows stack in, and the pins it owns
     */
    struct NodeView {
        Amethyst::Frame *frame = nullptr;
        Amethyst::Frame *body = nullptr;
        std::string title;
        GraphCategory category = GRAPH_CATEGORY_INPUT;
        Rapture::GraphNodeType type = Rapture::GraphNodeType::NONE;
        std::vector<uint32_t> pinIds;
        std::vector<Amethyst::UIObject *> rows;
        std::vector<Amethyst::UIObject *> valueFields;
        Amethyst::Frame *variantRow = nullptr;
        Amethyst::Dropdown *variantDropdown = nullptr;
        uint32_t nextRowOrder = 0;
        std::unique_ptr<TextureNodeData> textureData;
    };

    /**
     * @brief A connection from an output pin to an input pin, and the wire drawn between them
     */
    struct WireView {
        uint32_t srcPinId = 0;
        uint32_t dstPinId = 0;
        Amethyst::Spline *spline = nullptr;
    };

    void setupCanvas(void);
    void setupCanvasOverlays(void);
    void setupAddNodeMenu(void);
    void setupNodeMenu(void);

    /**
     * @brief Loads this panel's material into the canvas as a graph
     * @param handle The material asset to load
     */
    void loadMaterial(Rapture::AssetHandle handle);

    /**
     * @brief Rebuilds the canvas from an authored graph, laying its nodes out left to right
     * @param graph The source graph to reconstruct as editor nodes and wires
     */
    void loadGraph(const Rapture::MaterialGraph &graph);

    void onCanvasInputBegan(const Amethyst::InputObject &io);
    void onCanvasInputChanged(const Amethyst::InputObject &io);
    void onCanvasInputEnded(const Amethyst::InputObject &io);
    void onCanvasMouseWheel(const Amethyst::InputObject &io);

    /**
     * @brief Moves the view over the canvas, carrying the nodes and the dots under them
     * @param pan The content's offset from the canvas's top left
     */
    void setPan(Amethyst::vec2 pan);

    /**
     * @brief Sets how large the graph is drawn, keeping the content under a point of the canvas in place
     * @param canvasPoint The point to zoom around, relative to the canvas's top left
     * @param zoom The new zoom, clamped to the zoom range
     */
    void zoomAt(Amethyst::vec2 canvasPoint, float zoom);

    /**
     * @brief Spaces the two canvas dot layers for the current zoom, fading the fine layer out as it grows dense
     */
    void updateCanvasDotSpacing(void);

    /**
     * @brief Moves the view so every node sits in the middle of the canvas
     */
    void frameAllNodes(void);

    /**
     * @brief Opens the add node menu at a point on screen, adding the picked node there
     * @param screenPos Where the menu opens and the node is placed
     */
    void openAddNodeMenu(Amethyst::vec2 screenPos);

    /**
     * @brief Lists the nodes whose names match a search, grouped by category
     * @param search Text the node names are matched against, empty for every node
     */
    void fillAddNodeResults(std::string_view search);

    /**
     * @brief Adds the node a catalog entry describes at the add node menu's point and closes the menu
     * @param catalogIndex Index of the entry in the node catalog
     */
    void addNodeFromCatalog(size_t catalogIndex);

    /**
     * @brief Builds a node's frame, header and the body its rows stack in
     * @param canvasPos The node's top left in content space
     * @param title The header text
     * @param category The category tinting the header
     * @return The new node's id
     */
    uint32_t createNodeShell(Amethyst::vec2 canvasPos, std::string_view title, GraphCategory category);

    /**
     * @brief Spawns a graph node at a content space position
     * @param type The graph node type the node represents
     * @param canvasPos The node's top left in content space
     * @return The new node's id
     */
    uint32_t spawnNode(Rapture::GraphNodeType type, Amethyst::vec2 canvasPos);

    /**
     * @brief Spawns an editor-only texture node at a content space position
     * @param kind The texture source the node samples
     * @param title The node's header text
     * @param canvasPos The node's top left in content space
     * @return The new node's id
     */
    uint32_t spawnTextureNode(TextureNodeKind kind, std::string_view title, Amethyst::vec2 canvasPos);

    /**
     * @brief Fills a graph node's body with its variant selector, pins and value fields for its current type
     * @param nodeId The node to fill
     */
    void buildNodeRows(uint32_t nodeId);

    /**
     * @brief Fills an editor-only texture node's body with its pins, preview and generator parameters
     * @param nodeId The node to fill
     */
    void buildTextureNodeRows(uint32_t nodeId);

    /**
     * @brief Adds a row with the dropdown switching a node between its typed variants
     * @param nodeId The node
     */
    void addVariantRow(uint32_t nodeId);

    /**
     * @brief Adds one row per component of a constant's value, each a labelled field
     * @param nodeId The constant node
     */
    void addConstantRows(uint32_t nodeId);

    /**
     * @brief Adds a row holding a pin: its socket on the node's edge, its name, and an inline field for an unconnected value
     * @param nodeId The owning node
     * @param name The pin's name
     * @param type The type of value the pin carries
     * @param slotIndex The pin's index among the node's inputs or outputs
     * @param isOutput True for an output on the right edge, false for an input on the left
     * @param defaultValue The value an input starts on, unused for an output
     * @return The new pin's id
     */
    uint32_t addPinRow(uint32_t nodeId, std::string_view name, Rapture::PinType type, uint32_t slotIndex, bool isOutput,
                       const Rapture::PinValue &defaultValue);

    /**
     * @brief Adds a square preview of the texture a node samples
     * @param nodeId The texture node
     * @return The preview image
     */
    Amethyst::ImageLabel *addTexturePreview(uint32_t nodeId);

    /**
     * @brief Adds one reflected generator parameter to a procedural node as a labelled field
     * @param nodeId The procedural texture node
     * @param desc The reflected parameter the field edits
     * @param param The binding backing the field
     */
    void addTextureParamRow(uint32_t nodeId, const Rapture::ProceduralParameter &desc, TextureParam *param);

    /**
     * @brief Adds an empty row to a node's body
     * @param nodeId The node
     * @param height The row's height
     * @return The row's frame
     */
    Amethyst::Frame *addNodeRow(uint32_t nodeId, float height);

    /**
     * @brief Regenerates a procedural texture node's image from its current parameters
     * @param nodeId The procedural texture node
     */
    void regenerateProcedural(uint32_t nodeId);

    /**
     * @brief Binds a texture node to a texture and shows it in its preview
     * @param nodeId The texture node
     * @param texture The texture the node samples
     */
    void setImageTexture(uint32_t nodeId, Rapture::Ref<Rapture::ATexture> texture);

    /**
     * @brief Switches a node to a different typed variant in place, keeping the wires that still fit
     * @param nodeId The node to retype
     * @param newType The variant to switch to
     */
    void changeNodeType(uint32_t nodeId, Rapture::GraphNodeType newType);

    void connectPins(uint32_t srcNode, uint32_t srcSlot, uint32_t dstNode, uint32_t dstSlot);
    bool isInputConnected(uint32_t nodeId, uint32_t slotIndex) const;
    bool isPinConnected(uint32_t pinId) const;

    /**
     * @brief The pin whose socket is under a point on screen
     * @param screenPos The point
     * @return The pin's id, or INVALID_PIN if no socket is close enough
     */
    uint32_t findPinAt(Amethyst::vec2 screenPos) const;

    /**
     * @brief Finds a node's pin by direction and slot index
     * @param nodeId The owning node
     * @param isOutput True to match an output pin, false for an input
     * @param slotIndex The pin's index within that direction
     * @return The pin's id, or INVALID_PIN if the node has no such pin
     */
    uint32_t findPin(uint32_t nodeId, bool isOutput, uint32_t slotIndex) const;

    bool canConnect(uint32_t a, uint32_t b) const;
    /**
     * @brief Starts dragging a wire from a pin, or picks up the wire already in a connected input
     * @param pinId The pressed pin
     * @param screenPos Where the press happened
     */
    void beginConnection(uint32_t pinId, Amethyst::vec2 screenPos);
    void updateConnectionDrag(Amethyst::vec2 screenPos);
    void finishConnection(Amethyst::vec2 screenPos);

    /**
     * @brief Creates a wire between an output and an input, replacing the input's existing wire
     * @param outPinId The output pin
     * @param inPinId The input pin
     */
    void createWire(uint32_t outPinId, uint32_t inPinId);

    /**
     * @brief Builds a wire curve whose ends follow two pin sockets
     * @param from The pin the wire leaves
     * @param to The pin the wire enters
     * @return The wire's curve
     */
    Amethyst::Spline *createWireCurve(uint32_t from, uint32_t to);

    /**
     * @brief Removes a wire's curve and refreshes the pins it joined
     * @param wire The wire to remove
     */
    void destroyWire(const WireView &wire);

    /**
     * @brief Shows a pin as connected or not, and its inline field only while it is not
     * @param pinId The pin
     */
    void applyPinState(uint32_t pinId);

    void destroyNodeRows(uint32_t nodeId);

    /**
     * @brief Sets which pin is highlighted as the target of the wire being dragged
     * @param pinId The pin, or INVALID_PIN to clear the highlight
     */
    void setHoverPin(uint32_t pinId);

    /**
     * @brief Makes a node the primary selection, optionally keeping the current selection
     * @param nodeId The node to select
     * @param additive True to add to the selection, false to replace it
     */
    void selectNode(uint32_t nodeId, bool additive);
    void clearSelection(void);

    /**
     * @brief Sets the node shown as the selection and tells whoever shows it
     * @param nodeId The node, or 0 for none
     */
    void setPrimaryNode(uint32_t nodeId);

    /**
     * @brief Opens the menu acting on a node, selecting it first if it is not already
     * @param nodeId The node the menu acts on
     * @param screenPos Where the menu opens
     */
    void showNodeMenu(uint32_t nodeId, Amethyst::vec2 screenPos);

    void deleteNode(uint32_t nodeId);
    void deleteSelection(void);
    void clearGraph(void);

    /**
     * @brief Extracts a compiler-ready graph from the current editor state
     * @return The built graph, or an empty graph if there is no surface output node
     */
    Rapture::MaterialGraph buildGraph(void) const;

  private:
    Amethyst::Frame *m_canvas = nullptr;
    Amethyst::Frame *m_canvasFineDots = nullptr;
    Amethyst::Frame *m_canvasCoarseDots = nullptr;
    Amethyst::Frame *m_content = nullptr;
    Amethyst::Frame *m_wireLayer = nullptr;
    Amethyst::ContextMenu *m_nodeMenu = nullptr;

    Amethyst::Popup *m_addNodeMenu = nullptr;
    SearchField *m_addNodeSearch = nullptr;
    Amethyst::ScrollingFrame *m_addNodeResults = nullptr;
    std::vector<size_t> m_addNodeMatches;
    Amethyst::vec2 m_addNodeScreenPos{0.0f};

    Amethyst::EventConnection m_canvasBeganConn;
    Amethyst::EventConnection m_canvasMovedConn;
    Amethyst::EventConnection m_canvasEndedConn;
    Amethyst::EventConnection m_canvasWheelConn;

    bool m_panning = false;
    Amethyst::vec2 m_pan{0.0f};
    Amethyst::vec2 m_panLastMouse{0.0f};
    float m_zoom = 1.0f;
    Amethyst::vec2 m_canvasDotStep{0.0f};

    std::unordered_map<uint32_t, NodeView> m_nodes;
    Rapture::FreeList<PinView> m_pins;
    std::vector<WireView> m_connections;
    uint32_t m_nextNodeId = 1;

    bool m_connecting = false;
    uint32_t m_connectSrcPinId = 0;
    uint32_t m_hoverPinId = INVALID_PIN;
    Amethyst::Spline *m_dragWire = nullptr;

    std::unordered_set<uint32_t> m_selectedNodes;
    uint32_t m_primaryNodeId = 0;

    uint32_t m_selectedGraphId = UINT32_MAX;

    Rapture::EventSignal<void(Rapture::AssetHandle)> m_onMaterialSelectionChanged;
    Rapture::EventSignal<void(uint32_t)> m_onPrimaryNodeChanged;
    Rapture::EventSignal<void(uint32_t)> m_onNodeEdited;
    Rapture::EventSignal<void(uint32_t)> m_onNodeValuesChanged;
    Rapture::EventSignal<void(bool, size_t)> m_onCompiled;

    Rapture::AssetHandle m_material = Rapture::INVALID_ASSET_HANDLE;
};

#endif // RAPTURE__NODE_EDITOR_PANEL_H
