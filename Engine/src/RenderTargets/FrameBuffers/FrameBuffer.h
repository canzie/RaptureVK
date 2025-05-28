#pragma once

#include "RenderTargets/SwapChains/SwapChain.h"
#include "Renderpass.h"

#include <cstdint>
#include <vulkan/vulkan.h>
#include <vector>

namespace Rapture {



	struct FramebufferSpecification
	{

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t samples = 1;  // Multisampling: 1 = no multisampling
		std::vector<VkImageView> attachments = {};
		bool swapChainTarget = false;  // Whether this framebuffer is the main screen target
	};



class FrameBuffer {
public:
	// New primary constructor using Renderpass and subpass index
	//FrameBuffer(const Renderpass& renderpass, uint32_t subpassIndex, uint32_t width, uint32_t height, const std::vector<VkImageView>& imageViews);
	
	// Legacy constructors for compatibility
	FrameBuffer(const FramebufferSpecification& specification, VkRenderPass renderPass);
	FrameBuffer(const SwapChain& swapChain, uint32_t SCImageViewIndex, VkRenderPass renderPass);


    ~FrameBuffer();

    VkFramebuffer getFramebufferVk() const { return m_framebuffer; }

    void resize(uint32_t width, uint32_t height, std::vector<VkImageView> attachments);
    void resize(const SwapChain& swapChain, uint32_t SCImageViewIndex);

private:
    void invalidate();

private:
    FramebufferSpecification m_specification;
    VkFramebuffer m_framebuffer;
    VkRenderPass m_renderPass;
};


}