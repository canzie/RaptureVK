#include "LayerStack.h"

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
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex), std::move(layer));
    m_layerInsertIndex++;
    raw->attach();
    return raw;
}

Layer *LayerStack::pushOverlay(std::unique_ptr<Layer> overlay)
{
    if (overlay == nullptr) {
        return nullptr;
    }

    Layer *raw = overlay.get();
    m_layers.push_back(std::move(overlay));
    raw->attach();
    return raw;
}

void LayerStack::popLayer(Layer *layer)
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

void LayerStack::popOverlay(Layer *overlay)
{
    auto begin = m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex);
    auto it = std::find_if(begin, m_layers.end(), [overlay](const auto &held) { return held.get() == overlay; });
    if (it == m_layers.end()) {
        return;
    }

    overlay->detach();
    m_layers.erase(it);
}

void LayerStack::clear()
{
    // back to front, so an overlay is gone before the layers it was drawn over
    for (size_t i = m_layers.size(); i > 0; i--) {
        m_layers[i - 1]->detach();
    }

    m_layers.clear();
    m_layerInsertIndex = 0;
}

} // namespace Rapture
