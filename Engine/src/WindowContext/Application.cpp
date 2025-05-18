#include "Application.h"
#include "Logging/Log.h"

namespace Rapture {

	Application* Application::s_instance = nullptr;


    Application::Application(int width, int height, const char* title)
    : m_running(true)
    , m_isMinimized(false),
    m_vulkanContext(nullptr)
    {

        m_window = std::unique_ptr<WindowContext>(WindowContext::createWindow(width, height, title));
        m_vulkanContext = std::unique_ptr<VulkanContext>(new VulkanContext(m_window.get()));
		s_instance = this;

        ApplicationEvents::onWindowClose().addListener([this]() {
            m_running = false;
        });

        ApplicationEvents::onWindowFocus().addListener([this]() {
            RP_CORE_INFO("Window focused");
        });

        ApplicationEvents::onWindowLostFocus().addListener([this]() {
            RP_CORE_INFO("Window lost focus");
        });

        RP_CORE_INFO("========== Application created ==========");

    }

    Application::~Application() {

        // Shutdown the event system and clear all listeners
        EventRegistry::getInstance().shutdown();

        RP_CORE_INFO("Application shutting down...");
    }

    void Application::run() {

        while (m_running) {




            m_window->onUpdate();
        }

    }

}
