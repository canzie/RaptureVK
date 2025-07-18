#include "ImageViewerPanel.h"
#include "imgui_impl_vulkan.h"
#include "Logging/Log.h"

ImageViewerPanel::ImageViewerPanel()
{
}

ImageViewerPanel::~ImageViewerPanel()
{
    cleanupDescriptorSet();
}

void ImageViewerPanel::render() {
    ImGui::Begin("Image Viewer");

    // Create an invisible button that covers the entire content area to serve as drop target
    ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    ImVec2 minDropSize(200.0f, 150.0f);
    ImVec2 dropAreaSize(std::max(minDropSize.x, availableRegion.x), std::max(minDropSize.y, availableRegion.y));
    
    // Create invisible button for the drop area
    ImGui::InvisibleButton("##DropArea", dropAreaSize);
    
    // Set up drop target for texture assets
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            IM_ASSERT(payload->DataSize == sizeof(Rapture::AssetHandle));
            Rapture::AssetHandle droppedHandle = *(const Rapture::AssetHandle*)payload->Data;
            
            // Only update if it's a different texture
            if (droppedHandle != m_currentTextureHandle) {
                // Clean up the previous descriptor set
                cleanupDescriptorSet();
                
                // Get the texture asset
                m_texture = Rapture::AssetManager::getAsset<Rapture::Texture>(droppedHandle);
                m_currentTextureHandle = droppedHandle;
                
                if (m_texture) {
                    Rapture::RP_CORE_INFO("ImageViewerPanel: Loaded texture for viewing");
                    // Descriptor set will be created lazily in render loop
                } else {
                    Rapture::RP_CORE_ERROR("ImageViewerPanel: Failed to load texture asset");
                    m_currentTextureHandle = Rapture::AssetHandle(); // Reset handle
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    
    // Reset cursor position to draw content over the invisible button
    ImGui::SetCursorPos(ImGui::GetWindowContentRegionMin());

    // Render the texture if available
    if (m_texture && m_texture->isReadyForSampling()) {
        // Create descriptor set if needed
        if (m_textureDescriptorSet == VK_NULL_HANDLE) {
            createTextureDescriptor();
        }
        
        if (m_textureDescriptorSet != VK_NULL_HANDLE) {
            // Get texture dimensions
            const auto& spec = m_texture->getSpecification();
            
            // Display texture info and zoom controls
            ImGui::Text("Dimensions: %dx%d", spec.width, spec.height);
            ImGui::Text("Format: %d", static_cast<int>(spec.format));
            ImGui::Separator();
            ImGui::SliderFloat("Zoom", &m_zoomFactor, 0.1f, 10.0f, "%.2fx");
            ImGui::Separator();

            // Calculate display size based on zoom factor
            float displayWidth = static_cast<float>(spec.width) * m_zoomFactor;
            float displayHeight = static_cast<float>(spec.height) * m_zoomFactor;

            // Clamp display size to available region while maintaining aspect ratio
            ImVec2 currentAvailableRegion = ImGui::GetContentRegionAvail();
            float aspectRatio = static_cast<float>(spec.width) / static_cast<float>(spec.height);

            if (displayWidth > currentAvailableRegion.x) {
                displayWidth = currentAvailableRegion.x;
                displayHeight = displayWidth / aspectRatio;
            }
            if (displayHeight > currentAvailableRegion.y) {
                displayHeight = currentAvailableRegion.y;
                displayWidth = displayHeight * aspectRatio;
            }

            // Center the image horizontally
            float centerX = (currentAvailableRegion.x - displayWidth) * 0.5f;
            if (centerX > 0) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerX);
            }
            
            // Display the image
            ImGui::Image((ImTextureID)m_textureDescriptorSet, ImVec2(displayWidth, displayHeight));
            
            // Handle mouse wheel zoom
            if (ImGui::IsItemHovered()) {
                float mouseWheel = ImGui::GetIO().MouseWheel;
                if (mouseWheel != 0) {
                    m_zoomFactor += mouseWheel * 0.2f;
                    m_zoomFactor = std::max(0.1f, std::min(m_zoomFactor, 10.0f));
                }
            }
        }
    } else if (m_texture && !m_texture->isReadyForSampling()) {
        ImGui::Text("Loading texture...");
    } else {
        ImGui::Text("Drop a texture from the Content Browser to view it here.");
        ImGui::Spacing();
        ImGui::TextDisabled("(Drag and drop a texture asset to this panel)");
    }

    ImGui::End();
}

void ImageViewerPanel::cleanupDescriptorSet() {
    if (m_textureDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_textureDescriptorSet);
        m_textureDescriptorSet = VK_NULL_HANDLE;
    }
}

void ImageViewerPanel::createTextureDescriptor() {
    if (!m_texture || !m_texture->isReadyForSampling()) {
        Rapture::RP_CORE_WARN("ImageViewerPanel: Cannot create descriptor for texture that's not ready");
        return;
    }
    
    try {
        VkDescriptorImageInfo imageInfo = m_texture->getDescriptorImageInfo();
        m_textureDescriptorSet = ImGui_ImplVulkan_AddTexture(
            imageInfo.sampler,
            imageInfo.imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        
        if (m_textureDescriptorSet == VK_NULL_HANDLE) {
            Rapture::RP_CORE_ERROR("ImageViewerPanel: Failed to create ImGui descriptor set for texture");
        } else {
            Rapture::RP_CORE_INFO("ImageViewerPanel: Successfully created descriptor set for texture");
        }
    } catch (const std::exception& e) {
        Rapture::RP_CORE_ERROR("ImageViewerPanel: Exception while creating texture descriptor: {}", e.what());
    }
}
