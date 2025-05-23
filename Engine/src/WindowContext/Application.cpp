#include "Application.h"
#include "Logging/Log.h"

#include "Renderer/ForwardRenderer/ForwardRenderer.h"

namespace Rapture {

	Application* Application::s_instance = nullptr;


    Application::Application(int width, int height, const char* title)
    : m_running(true)
    , m_isMinimized(false),
    m_vulkanContext(nullptr)
    {

        RP_CORE_INFO("Creating window...");
        m_window = std::unique_ptr<WindowContext>(WindowContext::createWindow(width, height, title));

        RP_CORE_INFO("Creating Vulkan context...");
        m_vulkanContext = std::unique_ptr<VulkanContext>(new VulkanContext(m_window.get()));
		s_instance = this;

        ForwardRenderer::init();

        ApplicationEvents::onWindowClose().addListener([this]() {
            m_running = false;
        });

        ApplicationEvents::onWindowFocus().addListener([this]() {
            RP_CORE_INFO("Window focused");
        });

        ApplicationEvents::onWindowLostFocus().addListener([this]() {
            RP_CORE_INFO("Window lost focus");
        });

        ApplicationEvents::onWindowResize().addListener([this](unsigned int width, unsigned int height) {
            RP_CORE_INFO("Window resized to {}x{}", width, height);
        });

        RP_CORE_INFO("========== Application created ==========");

    }

    Application::~Application() {

        ForwardRenderer::shutdown();

        // Shutdown the event system and clear all listeners
        EventRegistry::getInstance().shutdown();

        RP_CORE_INFO("Application shutting down...");
    }

    void Application::run() {

        while (m_running) {

            m_window->onUpdate();

            ForwardRenderer::drawFrame();
            //m_vulkanContext->drawFrame(m_window.get());

        }

        m_vulkanContext->waitIdle();

    }

}
