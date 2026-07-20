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
     * @return Non-owning pointer to the created viewport
     */
    Viewport *createViewport(const ViewportConfig &config);

    void destroyViewport(const std::string &name);

    Viewport *getViewport(const std::string &name);
    Viewport *getPrimaryViewport() { return m_primaryViewport; }

    void drawAll();

    void onSwapChainRecreated();

    const std::vector<std::unique_ptr<Viewport>> &getViewports() const { return m_viewports; }

  private:
    RenderContext m_renderContext;
    std::vector<std::unique_ptr<Viewport>> m_viewports;
    Viewport *m_primaryViewport = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__VIEWPORT_MANAGER_H
