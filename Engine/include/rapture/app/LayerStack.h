#ifndef RAPTURE__LAYER_STACK_H
#define RAPTURE__LAYER_STACK_H

#include <rapture/app/Layer.h>

#include <memory>
#include <vector>

namespace Rapture {

enum LayerStackChange {
    LAYER_STACK_CHANGE_PUSH_LAYER,
    LAYER_STACK_CHANGE_PUSH_OVERLAY,
    LAYER_STACK_CHANGE_POP_LAYER,
    LAYER_STACK_CHANGE_POP_OVERLAY
};

/**
 * @brief Owns the layers of a frame, holding the overlays after the regular layers.
 *
 * Pushes and pops made while the stack is updating take effect once that update is done.
 */
class LayerStack {
  public:
    ~LayerStack();

    /**
     * @brief Takes ownership of a layer and attaches it
     * @param layer The layer to hold
     * @return The layer, owned by this stack
     */
    Layer *pushLayer(std::unique_ptr<Layer> layer);

    /**
     * @brief Takes ownership of an overlay and attaches it
     * @param overlay The overlay to hold
     * @return The overlay, owned by this stack
     */
    Layer *pushOverlay(std::unique_ptr<Layer> overlay);

    /**
     * @brief Detaches a layer and destroys it
     * @param layer The layer to drop, ignored if this stack does not hold it
     */
    void popLayer(Layer *layer);

    /**
     * @brief Detaches an overlay and destroys it
     * @param overlay The overlay to drop, ignored if this stack does not hold it
     */
    void popOverlay(Layer *overlay);

    /**
     * @brief Updates every attached regular layer
     * @param dt Seconds since the last frame
     */
    void updateLayers(float dt);

    /**
     * @brief Updates every attached overlay
     * @param dt Seconds since the last frame
     */
    void updateOverlays(float dt);

    void clear();

    using Iterator = std::vector<std::unique_ptr<Layer>>::iterator;
    using ConstIterator = std::vector<std::unique_ptr<Layer>>::const_iterator;

    Iterator layerBegin() { return m_layers.begin(); }
    Iterator layerEnd() { return m_layers.begin() + m_layerInsertIndex; }
    ConstIterator layerBegin() const { return m_layers.begin(); }
    ConstIterator layerEnd() const { return m_layers.begin() + m_layerInsertIndex; }

    Iterator overlayBegin() { return m_layers.begin() + m_layerInsertIndex; }
    Iterator overlayEnd() { return m_layers.end(); }
    ConstIterator overlayBegin() const { return m_layers.begin() + m_layerInsertIndex; }
    ConstIterator overlayEnd() const { return m_layers.end(); }

  private:
    struct PendingLayerStackChange {
        LayerStackChange change;
        std::unique_ptr<Layer> pushed = nullptr;
        Layer *popped = nullptr;
    };

  private:
    void insertLayer(std::unique_ptr<Layer> layer);
    void insertOverlay(std::unique_ptr<Layer> overlay);
    void removeLayer(Layer *layer);
    void removeOverlay(Layer *overlay);
    void applyPendingChanges();

  private:
    std::vector<std::unique_ptr<Layer>> m_layers;
    size_t m_layerInsertIndex = 0;
    bool m_updating = false;
    std::vector<PendingLayerStackChange> m_pendingChanges;
};

} // namespace Rapture

#endif // RAPTURE__LAYER_STACK_H
