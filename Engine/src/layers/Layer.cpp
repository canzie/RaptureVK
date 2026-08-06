#include "Layer.h"

namespace Rapture {

uint32_t Layer::m_nextLayerId = 0;

Layer::Layer(std::string_view name) : m_name(name.empty() ? "Layer_" + std::to_string(m_nextLayerId) : std::string(name))
{
    m_nextLayerId++;
}

void Layer::attach()
{
    if (m_attached) {
        return;
    }

    m_attached = true;
    onAttach();
}

void Layer::detach()
{
    if (!m_attached) {
        return;
    }

    m_attached = false;
    onDetach();
}

} // namespace Rapture
