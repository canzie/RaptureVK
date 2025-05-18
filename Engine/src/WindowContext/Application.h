#include "WindowContext.h"
#include "VulkanContext/VulkanContext.h"

#include <memory>

namespace Rapture {

    class Application {
    public:
		Application(int width, int height, const char* title);

        virtual ~Application();

        void run();

    private:
		bool m_running = true;
		bool m_isMinimized = false;


		std::unique_ptr<WindowContext> m_window;

		std::unique_ptr<VulkanContext> m_vulkanContext;

		static Application* s_instance;
    };


Application* CreateApplicationWindow(int width, int height, const char* title);

}