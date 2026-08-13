#ifndef RAPTURE__VIEWPORT_MANAGER_H
#define RAPTURE__VIEWPORT_MANAGER_H

#include "viewport/Viewport.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Rapture {

class ViewportManager {
  public:
    ViewportManager(RenderContext renderContext);
    ~ViewportManager() = default;

    ViewportManager(const ViewportManager &) = delete;
    ViewportManager &operator=(const ViewportManager &) = delete;

    /**
     * @brief Create a new viewport
     * @param config Creation-time configuration for the viewport and its render target
     * @return What identifies the created viewport, and the viewport itself
     */
    ViewportContext createViewport(const ViewportConfig &config);

    /**
     * @brief Destroy the viewport a context refers to, doing nothing when it refers to none
     * @param context What createViewport handed back
     */
    void destroyViewport(const ViewportContext &context);

    void drawAll();

    void onSwapChainRecreated();

    const std::vector<std::unique_ptr<Viewport>> &getViewports() const { return m_viewports; }

  private:
    RenderContext m_renderContext;
    std::vector<std::unique_ptr<Viewport>> m_viewports;
};

} // namespace Rapture

#endif // RAPTURE__VIEWPORT_MANAGER_H
