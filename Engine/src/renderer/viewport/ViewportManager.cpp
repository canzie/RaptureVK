#include "ViewportManager.h"

#include <algorithm>

namespace Rapture {

ViewportManager::ViewportManager(RenderContext renderContext) : m_renderContext(renderContext) {}

ViewportContext ViewportManager::createViewport(const ViewportConfig &config)
{
    auto viewport = std::make_unique<Viewport>(config, m_renderContext);
    auto *ptr = viewport.get();
    m_viewports.push_back(std::move(viewport));

    onViewportCreated.fire(ptr);

    return {ptr->getId(), config.name, ptr};
}

void ViewportManager::destroyViewport(const ViewportContext &context)
{
    auto it = std::find_if(m_viewports.begin(), m_viewports.end(),
                           [&context](const auto &vp) { return vp->getId() == context.id; });

    if (it != m_viewports.end()) {
        m_viewports.erase(it);
    }
}

void ViewportManager::drawAll()
{
    for (auto &vp : m_viewports) {
        if (!vp->isActive()) {
            continue;
        }
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
