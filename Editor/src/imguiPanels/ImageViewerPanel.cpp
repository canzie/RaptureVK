#include "ImageViewerPanel.h"
#include "IconsMaterialDesign.h"
#include "Logging/Log.h"
#include "imgui_impl_vulkan.h"

ImageViewerPanel::ImageViewerPanel() : m_uniqueId("Image Viewer") {}

ImageViewerPanel::ImageViewerPanel(Rapture::AssetHandle textureHandle, const std::string &uniqueId)
    : m_currentTextureHandle(textureHandle), m_uniqueId(uniqueId)
{
    if (textureHandle != Rapture::AssetHandle()) {
        m_textureAsset = Rapture::AssetManager::getAsset(textureHandle);
        m_texture = m_textureAsset ? m_textureAsset.get()->getUnderlyingAsset<Rapture::Texture>() : nullptr;
        if (m_texture) {
            Rapture::RP_CORE_INFO("Loaded texture for viewing in panel: {}", uniqueId);
        } else {
            Rapture::RP_CORE_ERROR("Failed to load texture asset for panel: {}", uniqueId);
            m_currentTextureHandle = Rapture::AssetHandle();
        }
    }
}

ImageViewerPanel::~ImageViewerPanel()
{
    cleanupDescriptorSet();
}

void ImageViewerPanel::setTextureHandle(Rapture::AssetHandle textureHandle)
{
    if (textureHandle != m_currentTextureHandle) {
        cleanupDescriptorSet();
        m_textureAsset = Rapture::AssetManager::getAsset(textureHandle);
        m_texture = m_textureAsset ? m_textureAsset.get()->getUnderlyingAsset<Rapture::Texture>() : nullptr;
        m_currentTextureHandle = textureHandle;
        if (m_texture) {
            Rapture::RP_CORE_INFO("Loaded texture for viewing");
        } else {
            Rapture::RP_CORE_ERROR("Failed to load texture asset");
            m_currentTextureHandle = Rapture::AssetHandle();
        }
    }
}

void ImageViewerPanel::render()
{
    setupInitialWindowSize();

    if (!ImGui::Begin(m_uniqueId.c_str(), &m_isOpen)) {
        ImGui::End();
        return;
    }

    if (isWindowDocked()) {
        handleDragAndDrop();
    }

    if (m_texture && m_texture->isReadyForSampling()) {
        if (m_textureDescriptorSet == VK_NULL_HANDLE) {
            createTextureDescriptor();
        }

        if (m_textureDescriptorSet != VK_NULL_HANDLE) {
            const auto &spec = m_texture->getSpecification();
            renderTextureInfo(spec);

            ImVec2 availableRegion = ImGui::GetContentRegionAvail();
            ImVec2 displaySize = calculateDisplaySize(spec, availableRegion);
            renderTextureImage(displaySize);
            handleMouseWheelZoom();
        }
    } else if (m_texture && !m_texture->isReadyForSampling()) {
        ImGui::Text("Loading texture...");
    } else {
        renderEmptyState();
    }

    ImGui::End();
}

void ImageViewerPanel::setupInitialWindowSize()
{
    if (!m_isFirstRender) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImVec2 windowSize = calculateWindowSizeFromTexture();
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
    m_isFirstRender = false;
}

ImVec2 ImageViewerPanel::calculateWindowSizeFromTexture() const
{
    const float DEFAULT_WIDTH = 800.0f;
    const float DEFAULT_HEIGHT = 600.0f;
    const float MIN_SIZE = 300.0f;
    const float MAX_SIZE = 1200.0f;

    if (!m_texture || !m_texture->isReadyForSampling()) {
        return ImVec2(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    }

    const auto &spec = m_texture->getSpecification();
    float aspectRatio = static_cast<float>(spec.width) / static_cast<float>(spec.height);

    float baseWidth = DEFAULT_WIDTH;
    float calculatedHeight = baseWidth / aspectRatio;

    // Clamp to reasonable sizes
    if (baseWidth > MAX_SIZE) {
        baseWidth = MAX_SIZE;
        calculatedHeight = baseWidth / aspectRatio;
    }
    if (calculatedHeight > MAX_SIZE) {
        calculatedHeight = MAX_SIZE;
        baseWidth = calculatedHeight * aspectRatio;
    }
    if (baseWidth < MIN_SIZE) {
        baseWidth = MIN_SIZE;
        calculatedHeight = baseWidth / aspectRatio;
    }
    if (calculatedHeight < MIN_SIZE) {
        calculatedHeight = MIN_SIZE;
        baseWidth = calculatedHeight * aspectRatio;
    }

    return ImVec2(baseWidth, calculatedHeight);
}

void ImageViewerPanel::handleDragAndDrop()
{
    ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    ImVec2 minDropSize(200.0f, 150.0f);
    ImVec2 dropAreaSize(std::max(minDropSize.x, availableRegion.x), std::max(minDropSize.y, availableRegion.y));

    ImGui::InvisibleButton("##DropArea", dropAreaSize);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET")) {
            IM_ASSERT(payload->DataSize == sizeof(Rapture::AssetHandle));
            Rapture::AssetHandle droppedHandle = *(const Rapture::AssetHandle *)payload->Data;

            if (droppedHandle != m_currentTextureHandle) {
                cleanupDescriptorSet();
                m_textureAsset = Rapture::AssetManager::getAsset(droppedHandle);
                m_texture = m_texture ? m_textureAsset.get()->getUnderlyingAsset<Rapture::Texture>() : nullptr;
                m_currentTextureHandle = droppedHandle;

                if (m_texture) {
                    Rapture::RP_CORE_INFO("Loaded texture for viewing");
                } else {
                    Rapture::RP_CORE_ERROR("Failed to load texture asset");
                    m_currentTextureHandle = Rapture::AssetHandle();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SetCursorPos(ImGui::GetWindowContentRegionMin());
}

void ImageViewerPanel::renderTextureInfo(const Rapture::TextureSpecification &spec)
{
    ImGui::Text("Dimensions: %dx%d", spec.width, spec.height);
    ImGui::Text("Format: %d", static_cast<int>(spec.format));
    ImGui::Separator();
    ImGui::SliderFloat("Zoom", &m_zoomFactor, 0.1f, 10.0f, "%.2fx");
    ImGui::Separator();
}

ImVec2 ImageViewerPanel::calculateDisplaySize(const Rapture::TextureSpecification &spec, const ImVec2 &availableRegion) const
{
    float displayWidth = static_cast<float>(spec.width) * m_zoomFactor;
    float displayHeight = static_cast<float>(spec.height) * m_zoomFactor;
    float aspectRatio = static_cast<float>(spec.width) / static_cast<float>(spec.height);

    if (displayWidth > availableRegion.x) {
        displayWidth = availableRegion.x;
        displayHeight = displayWidth / aspectRatio;
    }
    if (displayHeight > availableRegion.y) {
        displayHeight = availableRegion.y;
        displayWidth = displayHeight * aspectRatio;
    }

    return ImVec2(displayWidth, displayHeight);
}

void ImageViewerPanel::renderTextureImage(const ImVec2 &displaySize)
{
    ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    float centerX = (availableRegion.x - displaySize.x) * 0.5f;
    if (centerX > 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerX);
    }

    ImGui::Image((ImTextureID)m_textureDescriptorSet, displaySize);
}

void ImageViewerPanel::handleMouseWheelZoom()
{
    if (ImGui::IsItemHovered()) {
        float mouseWheel = ImGui::GetIO().MouseWheel;
        if (mouseWheel != 0) {
            m_zoomFactor += mouseWheel * 0.2f;
            m_zoomFactor = std::max(0.1f, std::min(m_zoomFactor, 10.0f));
        }
    }
}

bool ImageViewerPanel::isWindowDocked() const
{
    return ImGui::IsWindowDocked();
}

void ImageViewerPanel::renderEmptyState()
{
    if (isWindowDocked()) {
        ImGui::Text("Drop a texture from the Content Browser to view it here.");
        ImGui::Spacing();
        ImGui::TextDisabled("(Drag and drop a texture asset to this panel)");
    } else {
        ImGui::Text("No texture loaded.");
    }
}

void ImageViewerPanel::cleanupDescriptorSet()
{
    if (m_textureDescriptorSet != VK_NULL_HANDLE) {
        if (m_cleanupCallback) {
            m_cleanupCallback(m_textureDescriptorSet);
        }
        m_textureDescriptorSet = VK_NULL_HANDLE;
    }
}

void ImageViewerPanel::createTextureDescriptor()
{
    if (!m_texture || !m_texture->isReadyForSampling()) {
        Rapture::RP_CORE_WARN("Cannot create descriptor for texture that's not ready");
        return;
    }

    try {
        VkDescriptorImageInfo imageInfo = m_texture->getDescriptorImageInfo();
        m_textureDescriptorSet =
            ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_textureDescriptorSet == VK_NULL_HANDLE) {
            Rapture::RP_CORE_ERROR("Failed to create ImGui descriptor set for texture");
        } else {
            Rapture::RP_CORE_INFO("Successfully created descriptor set for texture");
        }
    } catch (const std::exception &e) {
        Rapture::RP_CORE_ERROR("Exception while creating texture descriptor: {}", e.what());
    }
}
