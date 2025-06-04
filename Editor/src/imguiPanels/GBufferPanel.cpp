#include "GBufferPanel.h"
#include "WindowContext/Application.h"
#include "imgui_impl_vulkan.h" // Required for ImGui_ImplVulkan_AddTexture/RemoveTexture
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

// Anonymous namespace for helper functions or constants
namespace {
    const int TEXTURES_PER_ROW = 2; // How many textures to display per row
}

GBufferPanel::~GBufferPanel() {
    // Ensure descriptor sets are cleaned up
    if (m_initialized) {
        for (auto& descSet : m_gbufferDescriptorSets) {
            if (descSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(descSet);
            }
        }
        m_gbufferDescriptorSets.clear();
    }
}

void GBufferPanel::renderTexture(const char* label, std::shared_ptr<Rapture::Texture> texture, VkDescriptorSet& descriptorSet) {
    if (!texture || !texture->getImageView() || !texture->getSampler().getSamplerVk()) {
        ImGui::TextWrapped("%s: (Texture data not available or invalid)", label);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        return;
    }

    if (descriptorSet == VK_NULL_HANDLE) {
        descriptorSet = ImGui_ImplVulkan_AddTexture(
            texture->getSampler().getSamplerVk(),
            texture->getImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        if (descriptorSet == VK_NULL_HANDLE) {
            Rapture::RP_CORE_ERROR("GBufferPanel: Failed to create ImGui descriptor set for texture: {}", label);
            ImGui::TextWrapped("%s: (Failed to create ImGui descriptor set)", label);
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
            return;
        }
    }

    ImGui::Text("%s", label);

    // Calculate display size maintaining aspect ratio
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float aspectRatio = static_cast<float>(texture->getSpecification().height) / static_cast<float>(texture->getSpecification().width);
    ImVec2 displaySize(availableWidth, availableWidth * aspectRatio);
    
    ImGui::Image((ImTextureID)descriptorSet, displaySize);
}

void GBufferPanel::render() {
    RAPTURE_PROFILE_FUNCTION();

    if (!m_initialized) {
        updateDescriptorSets(); // Initial setup
    }

    ImGui::Begin("G-Buffer Inspector");

    std::shared_ptr<Rapture::GBufferPass> gbufferPass = Rapture::DeferredRenderer::getGBufferPass();
    if (!gbufferPass) {
        ImGui::TextWrapped("G-Buffer pass not available. Ensure DeferredRenderer is initialized and a scene is rendering.");
        ImGui::End();
        return;
    }

    // Ensure we have enough descriptor sets. Should match the number of G-Buffer textures.
    // Typically: Position, Normal, Albedo, Material, Depth
    const size_t numTextures = 5; 
    if (m_gbufferDescriptorSets.size() != numTextures) {
        updateDescriptorSets(); // Attempt to re-initialize if size mismatch
        if (m_gbufferDescriptorSets.size() != numTextures) {
             ImGui::TextWrapped("Error: Could not initialize descriptor sets for all G-Buffer textures. Check logs.");
             ImGui::End();
             return;
        }
    }
    
    int currentTextureIndex = 0;

    ImGui::Columns(TEXTURES_PER_ROW, nullptr, false); // No borders for columns

    auto renderEntry = [&](const char* name, std::shared_ptr<Rapture::Texture> tex, const char* formatNote, const char* displayNote) {
        ImGui::BeginGroup();
        std::string fullLabel = std::string(name) + " " + formatNote;
        if (tex) {
            renderTexture(fullLabel.c_str(), tex, m_gbufferDescriptorSets[currentTextureIndex]);
        } else {
            ImGui::TextWrapped("%s: (Not Available)", fullLabel.c_str());
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        }
        ImGui::EndGroup();
        ImGui::NextColumn();
        currentTextureIndex++;
    };

    renderEntry("Position+Depth", gbufferPass->getPositionTexture(), "(RGBA32F)", 
                "World pos (RGB), View Z (A). Float data, direct view likely black/extreme.");
    renderEntry("Normal", gbufferPass->getNormalTexture(), "(RGBA16F)", 
                "World normal (RGB). Float data (-1 to 1), direct view likely dark.");
    renderEntry("Albedo+Specular", gbufferPass->getAlbedoTexture(), "(RGBA8 SRGB)", 
                "Albedo (RGB), Spec (A). Should be visible if materials are set.");
    renderEntry("Material Props", gbufferPass->getMaterialTexture(), "(RGBA8 UNORM)", 
                "Metallic (R), Roughness (G), AO (B). Should be visible.");
    renderEntry("Depth/Stencil View", gbufferPass->getDepthTexture(), "(D24S8)", 
                "Depth format. Cannot be directly viewed as color. This view is likely invalid.");

    ImGui::Columns(1); // Reset columns
    ImGui::End();
}

void GBufferPanel::updateDescriptorSets() {
    // Clean up existing descriptor sets
    for (auto& descSet : m_gbufferDescriptorSets) {
        if (descSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descSet);
        }
    }
    m_gbufferDescriptorSets.clear();

    std::shared_ptr<Rapture::GBufferPass> gbufferPass = Rapture::DeferredRenderer::getGBufferPass();
    if (!gbufferPass) {
        Rapture::RP_CORE_WARN("GBufferPanel::updateDescriptorSets - G-Buffer pass not available during update.");
        m_initialized = false;
        return;
    }

    // Recreate descriptor sets for all G-Buffer textures
    // The order here must match the order used in render()
    const size_t numTextures = 5; // Position, Normal, Albedo, Material, Depth
    m_gbufferDescriptorSets.resize(numTextures, VK_NULL_HANDLE);
    
    // Textures are created (or attempted) in renderTexture on first use if null.
    // This function primarily clears them. Re-creation will happen in renderTexture.
    // However, to be safe and ensure slots are there:
    auto initSetSlot = [&](std::shared_ptr<Rapture::Texture> tex, int idx) {
        // No need to call ImGui_ImplVulkan_AddTexture here, renderTexture will do it lazily.
        // This function is mostly for clearing now.
    };

    initSetSlot(gbufferPass->getPositionTexture(), 0);
    initSetSlot(gbufferPass->getNormalTexture(), 1);
    initSetSlot(gbufferPass->getAlbedoTexture(), 2);
    initSetSlot(gbufferPass->getMaterialTexture(), 3);
    initSetSlot(gbufferPass->getDepthTexture(), 4);
    
    m_initialized = true;
    // Rapture::RP_CORE_INFO("GBufferPanel: Descriptor sets prepared for lazy initialization.");
}
