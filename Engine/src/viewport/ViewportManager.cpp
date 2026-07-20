#include "ViewportManager.h"

#include <algorithm>

namespace Rapture {

ViewportManager::ViewportManager(RenderContext renderContext) : m_renderContext(renderContext) {}

Viewport *ViewportManager::createViewport(const ViewportConfig &config)
{
    auto viewport = std::make_unique<Viewport>(config, m_renderContext);
    auto *ptr = viewport.get();
    m_viewports.push_back(std::move(viewport));

    if (m_primaryViewport == nullptr) {
        m_primaryViewport = ptr;
    }

    return ptr;
}

void ViewportManager::destroyViewport(const std::string &name)
{
    auto it = std::find_if(m_viewports.begin(), m_viewports.end(), [&name](const auto &vp) { return vp->getName() == name; });

    if (it != m_viewports.end()) {
        if (m_primaryViewport == it->get()) {
            m_primaryViewport = nullptr;
        }
        m_viewports.erase(it);
    }
}

Viewport *ViewportManager::getViewport(const std::string &name)
{
    for (auto &vp : m_viewports) {
        if (vp->getName() == name) {
            return vp.get();
        }
    }
    return nullptr;
}

void ViewportManager::drawAll()
{
    for (auto &vp : m_viewports) {
        vp->drawFrame();
    }
}

void ViewportManager::onSwapChainRecreated()
{
    for (auto &vp : m_viewports) {
        vp->onSwapChainRecreated();
    }
}

} // namespace Rapture
