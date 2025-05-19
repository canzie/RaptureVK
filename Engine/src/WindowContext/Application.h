#pragma once

#include "WindowContext.h"
#include "VulkanContext/VulkanContext.h"

#include <memory>

namespace Rapture {

    class Application {
    public:
		Application(int width, int height, const char* title);

        virtual ~Application();

        void run();

        const VulkanContext& getVulkanContext() const { return *m_vulkanContext; }
        const WindowContext& getWindowContext() const { return *m_window; }
        static Application& getInstance() { return *s_instance; }

    private:
		bool m_running = true;
		bool m_isMinimized = false;


		std::unique_ptr<WindowContext> m_window;

		std::unique_ptr<VulkanContext> m_vulkanContext;

		static Application* s_instance;
    };


Application* CreateApplicationWindow(int width, int height, const char* title);

}