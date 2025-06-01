#pragma once

#include "WindowContext.h"
#include "VulkanContext/VulkanContext.h"

#include "Scenes/Scene.h"

#include "Layers/LayerStack.h"
#include "Scenes/Project.h"

#include <memory>

namespace Rapture {

    class Application {
    public:
		Application(int width, int height, const char* title);

        virtual ~Application();

        void run();

        void pushLayer(Layer* layer);
		void pushOverlay(Layer* overlay);

        const VulkanContext& getVulkanContext() const { return *m_vulkanContext; }
        VulkanContext& getVulkanContext() { return *m_vulkanContext; }
        const WindowContext& getWindowContext() const { return *m_window; }
        WindowContext& getWindowContext() { return *m_window; }

        static Application& getInstance() { return *s_instance; }



    private:
		bool m_running = true;
		bool m_isMinimized = false;

		LayerStack m_layerStack;

        std::unique_ptr<Project> m_project;

		std::unique_ptr<WindowContext> m_window;

		std::unique_ptr<VulkanContext> m_vulkanContext;

		static Application* s_instance;
    };


Application* CreateApplicationWindow(int width, int height, const char* title);

}