#ifndef RAPTURE__SCENE_RENDER_TARGET_H
#define RAPTURE__SCENE_RENDER_TARGET_H

#include "Textures/Texture.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include <memory>
#include <vector>

namespace Rapture {

/**
 * @brief Abstraction for scene render targets.
 * 
 * In Editor mode (OFFSCREEN), this manages a set of offscreen textures that the
 * renderer draws to. These can then be sampled by ImGui for display in the viewport.
 * 
 * In Standalone mode (SWAPCHAIN), this wraps the swapchain and provides access
 * to swapchain images directly.
 * 
 * This allows the renderer to be agnostic about where it's rendering to.
 */
class SceneRenderTarget {
public:
    enum class TargetType { 
        OFFSCREEN,  // Renders to separate textures (Editor mode)
        SWAPCHAIN   // Renders directly to swapchain (Standalone mode)
    };

    /**
     * @brief Construct an offscreen render target (Editor mode)
     * @param width Initial width of the render target
     * @param height Initial height of the render target
     * @param imageCount Number of images (typically matches frames in flight)
     * @param format The texture format for the render target
     */
    SceneRenderTarget(uint32_t width, uint32_t height, uint32_t imageCount, 
                      TextureFormat format = TextureFormat::BGRA8);

    /**
     * @brief Construct a swapchain-backed render target (Standalone mode)
     * @param swapChain The swapchain to wrap
     */
    explicit SceneRenderTarget(std::shared_ptr<SwapChain> swapChain);

    ~SceneRenderTarget();

    // Prevent copying
    SceneRenderTarget(const SceneRenderTarget&) = delete;
    SceneRenderTarget& operator=(const SceneRenderTarget&) = delete;

    /**
     * @brief Resize the render target (only valid for OFFSCREEN type)
     * @param width New width
     * @param height New height
     */
    void resize(uint32_t width, uint32_t height);

    /**
     * @brief Called when swapchain is recreated (updates internal reference)
     * Only relevant for SWAPCHAIN type, but safe to call on OFFSCREEN type.
     */
    void onSwapChainRecreated();

    // Getters
    VkImage getImage(uint32_t index) const;
    VkImageView getImageView(uint32_t index) const;
    VkFormat getFormat() const;
    VkExtent2D getExtent() const;
    uint32_t getImageCount() const;
    TargetType getType() const { return m_type; }

    /**
     * @brief Get the texture at the given index (only valid for OFFSCREEN type)
     * Used by ImGui to sample the rendered scene.
     * @param index The image index
     * @return Shared pointer to the texture, or nullptr for SWAPCHAIN type
     */
    std::shared_ptr<Texture> getTexture(uint32_t index) const;

    /**
     * @brief Check if this render target needs image layout transitions for sampling
     * OFFSCREEN targets need to transition to SHADER_READ_ONLY_OPTIMAL for ImGui sampling.
     * SWAPCHAIN targets need to transition to PRESENT_SRC_KHR for presentation.
     */
    bool requiresSamplingTransition() const { return m_type == TargetType::OFFSCREEN; }

    /**
     * @brief Transition the render target to shader read layout for sampling (OFFSCREEN only)
     * This transitions from COLOR_ATTACHMENT_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
     * so ImGui can sample the rendered scene.
     * @param commandBuffer The command buffer to record the transition to
     * @param imageIndex The image index to transition
     */
    void transitionToShaderReadLayout(const std::shared_ptr<CommandBuffer>& commandBuffer, uint32_t imageIndex);

private:
    void createOffscreenTextures(uint32_t width, uint32_t height, uint32_t imageCount, TextureFormat format);

private:
    TargetType m_type;
    
    // For OFFSCREEN mode
    std::vector<std::shared_ptr<Texture>> m_offscreenTextures;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    TextureFormat m_format = TextureFormat::BGRA8;
    
    // For SWAPCHAIN mode
    std::shared_ptr<SwapChain> m_swapChain;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_RENDER_TARGET_H
