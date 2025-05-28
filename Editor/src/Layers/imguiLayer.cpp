#include "imguiLayer.h"
#include "WindowContext/Application.h"
#include "Events/ApplicationEvents.h"
#include <stdlib.h>         // abort
//#include "Renderer/ForwardRenderer/ForwardRenderer.h"
#include "Logging/Log.h"



static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    //Rapture::RP_ERROR("[vulkan] Error: VkResult = {0}", err);
    if (err < 0)
        abort();
}

ImGuiLayer::ImGuiLayer()
{
}



void ImGuiLayer::onAttach()
{
    Rapture::RP_INFO("ImGuiLayer::onAttach");

    // Create Framebuffers
    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto& window = app.getWindowContext();

	//1: create descriptor pool for IMGUI
	// the size of the pool is very oversize, but it's copied from imgui demo itself.
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &pool_info, nullptr, &m_imguiPool);



      // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan((GLFWwindow*)window.getNativeWindowContext(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    //init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    init_info.Instance = vulkanContext.getInstance();
    init_info.PhysicalDevice = vulkanContext.getPhysicalDevice();
    init_info.Device = vulkanContext.getLogicalDevice();
    init_info.QueueFamily = vulkanContext.getQueueFamilyIndices().graphicsFamily.value();
    init_info.Queue = vulkanContext.getGraphicsQueue()->getQueueVk();
    init_info.DescriptorPool = m_imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    //init_info.RenderPass = Rapture::ForwardRenderer::getRenderpass()->getRenderPassVk();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info); 


}

void ImGuiLayer::onDetach()
{
}



void ImGuiLayer::onUpdate(float ts)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

	ImGui::NewFrame();


    //imgui commands
    ImGui::ShowDemoWindow();

    ImGui::EndFrame();

}
