#include <rapture/app/LayerStack.h>

#include <algorithm>

namespace Rapture {

LayerStack::~LayerStack()
{
    clear();
}

Layer *LayerStack::pushLayer(std::unique_ptr<Layer> layer)
{
    if (layer == nullptr) {
        return nullptr;
    }

    Layer *raw = layer.get();
    if (m_updating) {
        m_pendingChanges.push_back({.change = LAYER_STACK_CHANGE_PUSH_LAYER, .pushed = std::move(layer)});
        return raw;
    }

    insertLayer(std::move(layer));
    return raw;
}

Layer *LayerStack::pushOverlay(std::unique_ptr<Layer> overlay)
{
    if (overlay == nullptr) {
        return nullptr;
    }

    Layer *raw = overlay.get();
    if (m_updating) {
        m_pendingChanges.push_back({.change = LAYER_STACK_CHANGE_PUSH_OVERLAY, .pushed = std::move(overlay)});
        return raw;
    }

    insertOverlay(std::move(overlay));
    return raw;
}

void LayerStack::popLayer(Layer *layer)
{
    if (m_updating) {
        m_pendingChanges.push_back({.change = LAYER_STACK_CHANGE_POP_LAYER, .popped = layer});
        return;
    }

    removeLayer(layer);
}

void LayerStack::popOverlay(Layer *overlay)
{
    if (m_updating) {
        m_pendingChanges.push_back({.change = LAYER_STACK_CHANGE_POP_OVERLAY, .popped = overlay});
        return;
    }

    removeOverlay(overlay);
}

void LayerStack::updateLayers(float dt)
{
    m_updating = true;
    for (auto it = layerBegin(); it != layerEnd(); ++it) {
        if ((*it)->isAttached()) {
            (*it)->onUpdate(dt);
        }
    }
    m_updating = false;

    applyPendingChanges();
}

void LayerStack::updateOverlays(float dt)
{
    m_updating = true;
    for (auto it = overlayBegin(); it != overlayEnd(); ++it) {
        if ((*it)->isAttached()) {
            (*it)->onUpdate(dt);
        }
    }
    m_updating = false;

    applyPendingChanges();
}

void LayerStack::clear()
{
    m_pendingChanges.clear();

    // back to front, so an overlay is gone before the layers it was drawn over
    for (size_t i = m_layers.size(); i > 0; i--) {
        m_layers[i - 1]->detach();
    }

    m_layers.clear();
    m_layerInsertIndex = 0;
}

void LayerStack::insertLayer(std::unique_ptr<Layer> layer)
{
    Layer *raw = layer.get();
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex), std::move(layer));
    m_layerInsertIndex++;
    raw->attach();
}

void LayerStack::insertOverlay(std::unique_ptr<Layer> overlay)
{
    Layer *raw = overlay.get();
    m_layers.push_back(std::move(overlay));
    raw->attach();
}

void LayerStack::removeLayer(Layer *layer)
{
    auto end = m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex);
    auto it = std::find_if(m_layers.begin(), end, [layer](const auto &held) { return held.get() == layer; });
    if (it == end) {
        return;
    }

    layer->detach();
    m_layers.erase(it);
    m_layerInsertIndex--;
}

void LayerStack::removeOverlay(Layer *overlay)
{
    auto begin = m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex);
    auto it = std::find_if(begin, m_layers.end(), [overlay](const auto &held) { return held.get() == overlay; });
    if (it == m_layers.end()) {
        return;
    }

    overlay->detach();
    m_layers.erase(it);
}

void LayerStack::applyPendingChanges()
{
    std::vector<PendingLayerStackChange> changes = std::move(m_pendingChanges);
    m_pendingChanges.clear();

    for (PendingLayerStackChange &change : changes) {
        switch (change.change) {
        case LAYER_STACK_CHANGE_PUSH_LAYER:
            insertLayer(std::move(change.pushed));
            break;
        case LAYER_STACK_CHANGE_PUSH_OVERLAY:
            insertOverlay(std::move(change.pushed));
            break;
        case LAYER_STACK_CHANGE_POP_LAYER:
            removeLayer(change.popped);
            break;
        case LAYER_STACK_CHANGE_POP_OVERLAY:
            removeOverlay(change.popped);
            break;
        }
    }
}

} // namespace Rapture
