#include "GBufferPanel.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "WindowContext/Application.h"
#include "imgui_impl_vulkan.h" // Required for ImGui_ImplVulkan_AddTexture/RemoveTexture
#include "modules/BetterPrimitives.h"
#include "themes/imguiPanelStyle.h"

// Anonymous namespace for helper functions or constants
namespace {
const int TEXTURES_PER_ROW = 2; // How many textures to display per row
}

GBufferPanel::~GBufferPanel()
{
    // Ensure descriptor sets are cleaned up
    if (m_initialized) {
        for (auto &descSet : m_gbufferDescriptorSets) {
            if (descSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(descSet);
            }
        }
        m_gbufferDescriptorSets.clear();
    }
}

void GBufferPanel::renderTexture(const char *label, std::shared_ptr<Rapture::Texture> texture, VkDescriptorSet &descriptorSet)
{
    if (!texture) {
        ImGui::TextWrapped("%s: (Texture data not available or invalid)", label);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        return;
    }

    // Determine which image view to use based on the label
    VkImageView imageView = texture->getImageView();
    if (strstr(label, "Depth View") && texture->getDepthOnlyImageView()) {
        imageView = texture->getDepthOnlyImageView();
    } else if (strstr(label, "Stencil View") && texture->getStencilOnlyImageView()) {
        imageView = texture->getStencilOnlyImageView();
    }

    if (!imageView || !texture->getSampler().getSamplerVk()) {
        ImGui::TextWrapped("%s: (Texture view or sampler not available)", label);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        return;
    }

    if (descriptorSet == VK_NULL_HANDLE) {
        descriptorSet =
            ImGui_ImplVulkan_AddTexture(texture->getSampler().getSamplerVk(), imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (descriptorSet == VK_NULL_HANDLE) {
            Rapture::RP_CORE_ERROR("Failed to create ImGui descriptor set for texture: {}", label);
            ImGui::TextWrapped("%s: (Failed to create ImGui descriptor set)", label);
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
            return;
        }
    }

    ImGui::Text("%s", label);

    // Calculate display size maintaining aspect ratio
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float aspectRatio =
        static_cast<float>(texture->getSpecification().height) / static_cast<float>(texture->getSpecification().width);
    ImVec2 displaySize(availableWidth, availableWidth * aspectRatio);

    ImGui::Image((ImTextureID)descriptorSet, displaySize);
}

void GBufferPanel::render()
{
    RAPTURE_PROFILE_FUNCTION();

    std::shared_ptr<Rapture::GBufferPass> gbufferPass = Rapture::DeferredRenderer::getGBufferPass();
    if (!gbufferPass) {
        if (!BetterUi::BeginPanel("G-Buffer Inspector")) {
            BetterUi::EndPanel();
            return;
        }
        BetterUi::BeginContent();
        ImGui::TextWrapped("G-Buffer pass not available. Ensure DeferredRenderer is initialized and a scene is rendering.");
        BetterUi::EndContent();
        BetterUi::EndPanel();
        return;
    }

    // Calculate expected number of textures
    size_t expectedTextures = 4; // Position, Normal, Albedo, Material
    auto depthTexture = gbufferPass->getDepthTexture();
    if (depthTexture && Rapture::hasStencilComponent(depthTexture->getSpecification().format)) {
        expectedTextures += 2; // Additional views for depth and stencil
    } else {
        expectedTextures += 1; // Single depth view
    }

    if (!m_initialized) {
        updateDescriptorSets();
    }

    // Ensure we have enough descriptor sets
    if (m_gbufferDescriptorSets.size() != expectedTextures) {
        updateDescriptorSets();
        if (m_gbufferDescriptorSets.size() != expectedTextures) {
            if (!BetterUi::BeginPanel("G-Buffer Inspector")) {
                BetterUi::EndPanel();
                return;
            }
            BetterUi::BeginContent();
            ImGui::TextWrapped("Error: Could not initialize descriptor sets for all G-Buffer textures. Check logs.");
            BetterUi::EndContent();
            BetterUi::EndPanel();
            return;
        }
    }

    // Detect texture changes (e.g., after resize) and invalidate all descriptor sets
    std::vector<std::shared_ptr<Rapture::Texture>> currentTextures = {
        gbufferPass->getPositionTexture(), gbufferPass->getNormalTexture(), gbufferPass->getAlbedoTexture(),
        gbufferPass->getMaterialTexture()};
    if (depthTexture) {
        currentTextures.push_back(depthTexture);
        if (Rapture::hasStencilComponent(depthTexture->getSpecification().format)) {
            currentTextures.push_back(depthTexture);
        }
    }

    bool texturesChanged = m_cachedTextures.size() != currentTextures.size();
    if (!texturesChanged) {
        for (size_t i = 0; i < currentTextures.size(); i++) {
            if (m_cachedTextures[i] != currentTextures[i]) {
                texturesChanged = true;
                break;
            }
        }
    }

    if (texturesChanged) {
        for (auto &descSet : m_gbufferDescriptorSets) {
            if (descSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(descSet);
                descSet = VK_NULL_HANDLE;
            }
        }
        m_cachedTextures = currentTextures;
    }

    if (!BetterUi::BeginPanel("G-Buffer Inspector")) {
        BetterUi::EndPanel();
        return;
    }

    BetterUi::BeginContent();

    int currentTextureIndex = 0;

    ImGui::Columns(TEXTURES_PER_ROW, nullptr, false); // No borders for columns

    auto renderEntry = [&](const char *name, std::shared_ptr<Rapture::Texture> tex, const char *formatNote,
                           const char *displayNote) {
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

    // Special handling for depth texture to show both depth and stencil views if available
    if (depthTexture && Rapture::hasStencilComponent(depthTexture->getSpecification().format)) {
        // Depth-only view
        ImGui::BeginGroup();
        std::string depthLabel = "Depth View (D24S8)";
        if (depthTexture->getDepthOnlyImageView()) {
            renderTexture(depthLabel.c_str(), depthTexture, m_gbufferDescriptorSets[currentTextureIndex]);
        } else {
            ImGui::TextWrapped("%s: (Depth view not available)", depthLabel.c_str());
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        }
        ImGui::EndGroup();
        ImGui::NextColumn();
        currentTextureIndex++;

        // Stencil-only view
        ImGui::BeginGroup();
        std::string stencilLabel = "Stencil View (D24S8)";
        if (depthTexture->getStencilOnlyImageView()) {
            renderTexture(stencilLabel.c_str(), depthTexture, m_gbufferDescriptorSets[currentTextureIndex]);
        } else {
            ImGui::TextWrapped("%s: (Stencil view not available)", stencilLabel.c_str());
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
        }
        ImGui::EndGroup();
        ImGui::NextColumn();
        currentTextureIndex++;
    } else {
        // Regular depth texture display
        renderEntry("Depth View", depthTexture, "(D24S8)",
                    "Normalized depth (D24_UNORM). Displayed as Red channel (Red=far, Black=near).");
    }

    ImGui::Columns(1); // Reset columns

    BetterUi::EndContent();
    BetterUi::EndPanel();
}

void GBufferPanel::updateDescriptorSets()
{
    // Clean up existing descriptor sets
    for (auto &descSet : m_gbufferDescriptorSets) {
        if (descSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descSet);
        }
    }
    m_gbufferDescriptorSets.clear();

    std::shared_ptr<Rapture::GBufferPass> gbufferPass = Rapture::DeferredRenderer::getGBufferPass();
    if (!gbufferPass) {
        Rapture::RP_CORE_WARN("G-Buffer pass not available during update.");
        m_initialized = false;
        return;
    }

    // Calculate total number of textures needed
    size_t numTextures = 4; // Position, Normal, Albedo, Material
    auto depthTexture = gbufferPass->getDepthTexture();
    if (depthTexture && Rapture::hasStencilComponent(depthTexture->getSpecification().format)) {
        numTextures += 2; // Additional views for depth and stencil
    } else {
        numTextures += 1; // Single depth view
    }

    // Resize descriptor sets array
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

    // Handle depth texture
    if (depthTexture) {
        if (Rapture::hasStencilComponent(depthTexture->getSpecification().format)) {
            // Both depth and stencil views
            initSetSlot(depthTexture, 4); // Depth view
            initSetSlot(depthTexture, 5); // Stencil view
        } else {
            // Single depth view
            initSetSlot(depthTexture, 4);
        }
    }

    m_initialized = true;
}
