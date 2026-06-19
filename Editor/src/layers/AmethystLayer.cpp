#include "AmethystLayer.h"

#include "EditorLayout.h"
#include "buffers/command_buffers/CommandPool.h"
#include "events/ApplicationEvents.h"
#include "layers/panels/ContentBrowserPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "layers/panels/components/tab_layouts.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "scenes/SceneManager.h"
#include "viewport/ViewportManager.h"
#include "window_context/Application.h"

#include <components/common.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/image_label.h>
#include <components/ui_scope.h>
#include <modules/color.h>
#include <modules/style.h>
#include <parsers/config/layout_config.h>
#include <parsers/ttf/ttf_parser.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

AmethystLayer::AmethystLayer()
{
    Amethyst::Log::Init();

    auto &app = Rapture::Application::getInstance();
    auto rootPath = app.getProject().getProjectRootDirectory();
    auto themePath = rootPath / "assets/themes/theme.ams";
    Amethyst::Style::load(themePath);

    m_windowResizeEventListenerID = Rapture::ApplicationEvents::onWindowResize().addListener(
        [this](unsigned int width, unsigned int height) { m_framebufferNeedsResize = true; });

    Rapture::ApplicationEvents::onSwapChainRecreated().addListener(
        [this](std::shared_ptr<Rapture::SwapChain> swapChain) { onResize(); });
}

AmethystLayer::~AmethystLayer()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    for (auto &ws : m_workspaces) {
        if (ws.dockingLayer != nullptr && !ws.dockingLayer->name.empty()) {
            Amethyst::LayoutConfig::instance().set(ws.dockingLayer->name, Amethyst::ConfigEntry(ws.dockingLayer->saveConfig()));
        }
        ws.panels.clear();
    }
    Amethyst::LayoutConfig::instance().save();

    for (auto &texId : m_viewportTextureIds) {
        if (texId.isValid()) {
            m_backend.unregisterTexture(texId);
        }
    }

    m_backend.shutdown();
    Amethyst::Log::Shutdown();

    Rapture::RP_INFO("---Closing AmethystLayer---");
}

void AmethystLayer::onAttach()
{
    Rapture::RP_INFO("Attaching AmethystLayer...");

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto &window = app.getWindowContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &pool_info, nullptr, &descriptorPool);

    auto rootPath = app.getProject().getProjectRootDirectory();
    auto vertShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.vs.spv").string();
    auto fragShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.fs.spv").string();

    Amethyst::AmVulkanInitInfo initInfo{};
    initInfo.device = vulkanContext.getLogicalDevice();
    initInfo.instance = vulkanContext.getInstance();
    initInfo.physicalDevice = vulkanContext.getPhysicalDevice();
    initInfo.queue = vulkanContext.getVendorQueue()->getQueueVk();
    initInfo.queueFamiliy = vulkanContext.getGraphicsQueueIndex();
    initInfo.pool = descriptorPool;
    initInfo.minImageCount = swapChain->getImageCount();
    initInfo.imageCount = swapChain->getImageCount();
    initInfo.colorFormat = swapChain->getImageFormat();
    initInfo.extent = swapChain->getExtent();
    initInfo.vertexShaderPath = vertShaderPath.c_str();
    initInfo.fragmentShaderPath = fragShaderPath.c_str();

    Amethyst::AmGlfwInitInfo glfwInfo{};
    glfwInfo.window = static_cast<GLFWwindow *>(window.getNativeWindowContext());

    m_backend.init(initInfo, glfwInfo);

    auto fontPath = rootPath / "assets/fonts/Roboto-Regular.ttf";
    if (!m_amCtx.loadFont(fontPath.string())) {
        Rapture::RP_WARN("Failed to load font from: {}", fontPath.string());
    }

    m_amCtx.init(m_backend);

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    m_window.absoluteSize = screenSize;
    m_window.absoluteRotation = 0.0f;

    m_backgroundFrame = m_window.add<Amethyst::Frame>();
    m_backgroundFrame->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
        .zIndex = 0,
    });
    m_backgroundFrame->setBaseStyleProperties({.backgroundColor = Amethyst::Color3::fromHex(0x181818)});

    setupMenuBar(screenSize);
    setupWorkspaces(screenSize);

    m_bottomBar = m_window.add<Amethyst::Frame>();
    m_bottomBar->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -EDITOR_BOTTOM_BAR_HEIGHT),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_BOTTOM_BAR_HEIGHT),
    });

    auto activeScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (activeScene != nullptr) {
        for (auto &ws : m_workspaces) {
            for (auto &panel : ws.panels) {
                if (auto *outliner = dynamic_cast<OutlinerPanel *>(panel.get()); outliner != nullptr) {
                    outliner->setScene(activeScene);
                }
                if (auto *properties = dynamic_cast<PropertiesPanel *>(panel.get()); properties != nullptr) {
                    properties->setScene(activeScene);
                }
            }
        }
    }

    m_viewportTextureIds.resize(swapChain->getImageCount());

    Rapture::CommandPoolConfig config;
    config.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    config.flags = 0;
    config.threadId = 0;

    m_commandPoolHash = vulkanContext.getRenderContext().commandPoolManager->createCommandPool(config);
    m_imageCount = swapChain->getImageCount();
    m_currentFrame = 0;
}

void AmethystLayer::onDetach()
{
    Rapture::RP_INFO("Detaching AmethystLayer...");
}

void AmethystLayer::onUpdate(float ts)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();
    auto graphicsQueue = vulkanContext.getGraphicsQueue();

    int imageIndexi = swapChain->acquireImage(m_currentFrame);

    if (imageIndexi == -1) {
        m_currentFrame = 0;
        graphicsQueue->clear();
        onResize();
        m_framebufferNeedsResize = false;
        return;
    }

    m_currentImageIndex = static_cast<uint32_t>(imageIndexi);

    for (auto &ws : m_workspaces) {
        for (auto &panel : ws.panels) {
            panel->onUpdate(ts);
        }
    }

    m_window.tick(ts);

    VkSemaphore imageAvailableSemaphore = swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore renderFinishedSemaphore = swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    auto *sceneViewport = app.getViewportManager().getPrimaryViewport();
    auto *sceneRenderTarget = sceneViewport != nullptr ? sceneViewport->getSceneRenderTarget() : nullptr;
    if (sceneRenderTarget != nullptr) {
        uint32_t renderedSlot = sceneViewport->getLastRenderedFrameIndex();
        if (renderedSlot < m_viewportTextureIds.size()) {
            if (!m_viewportTextureIds[renderedSlot].isValid()) {
                auto texture = sceneRenderTarget->getTexture(renderedSlot);
                if (texture != nullptr) {
                    m_viewportTextureIds[renderedSlot] =
                        m_backend.registerTexture(texture->getImageView(), texture->getSampler().getSamplerVk());
                }
            }

            if (m_viewportTextureIds[renderedSlot].isValid()) {
                for (auto &ws : m_workspaces) {
                    for (auto &panel : ws.panels) {
                        if (auto *vp = dynamic_cast<ViewportPanel *>(panel.get()); vp != nullptr) {
                            vp->setViewportImage(m_viewportTextureIds[renderedSlot]);
                        }
                    }
                }
            }
        }
    }

    auto pool = vulkanContext.getRenderContext().commandPoolManager->getCommandPool(m_commandPoolHash, m_currentFrame);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    if (commandBuffer->begin(0) != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to begin recording command buffer for Amethyst!");
        return;
    }

    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    m_amCtx.draw(m_window);
    m_amCtx.sync(static_cast<void *>(cmd));

    auto targetImageView = swapChain->getImageViews()[m_currentImageIndex];

    beginDynamicRendering(commandBuffer, targetImageView);

    m_backend.record(cmd, m_amCtx.getDrawList());

    endDynamicRendering(commandBuffer);

    if (commandBuffer->end() != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to record command buffer for Amethyst!");
        return;
    }

    std::span<VkSemaphore> waitSemaphoresSpan(&imageAvailableSemaphore, 1);
    std::span<VkSemaphore> signalSemaphoresSpan(&renderFinishedSemaphore, 1);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    graphicsQueue->submitAndFlushQueue(commandBuffer, &signalSemaphoresSpan, &waitSemaphoresSpan, &waitStage,
                                       swapChain->getInFlightFence(m_currentFrame));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = vulkanContext.getPresentQueue()->presentQueue(presentInfo);
    swapChain->signalImageAvailability(m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {
        Rapture::ApplicationEvents::onRequestSwapChainRecreation().publish();
        m_framebufferNeedsResize = false;
        m_currentFrame = 0;
        onResize();
        return;
    } else if (result != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to present swap chain image!");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_imageCount;
}

void AmethystLayer::setupMenuBar(glm::vec2 screenSize)
{
    Amethyst::UIScope(m_window).menuBar(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_MENU_BAR_HEIGHT),
                },
            .style =
                {
                    .backgroundColor = Amethyst::Color3::fromHex(0x181818),
                    .backgroundTransparency = 0.0f,
                    .borderPixelSize = 0.0f,
                },
        },
        [this](Amethyst::MenuBarScope &mb) {
            m_menuBar = &mb.component;
            mb.menuItem("File", [](Amethyst::DropdownScope &d) {
                d.action("New Scene", [] {});
                d.action("Open Scene", [] {});
                d.action("Save Scene", [] {});
                d.separator();
                d.action("New Project", [] {});
                d.action("Open Project", [] {});
                d.separator();
                d.action("Exit", [] {});
            });
            mb.menuItem("Edit", [](Amethyst::DropdownScope &d) {
                d.action("Undo", [] {});
                d.action("Redo", [] {});
                d.separator();
                d.action("Cut", [] {});
                d.action("Copy", [] {});
                d.action("Paste", [] {});
                d.separator();
                d.action("Editor Preferences", [] {});
            });
            mb.menuItem("Window", [](Amethyst::DropdownScope &d) {
                d.action("Viewport", [] {});
                d.action("Outliner", [] {});
                d.action("Properties", [] {});
                d.action("Content Browser", [] {});
            });
            mb.menuItem("Help", [](Amethyst::DropdownScope &d) {
                d.action("Documentation", [] {});
                d.action("About", [] {});
            });
        });
}

void AmethystLayer::setupWorkspaces(glm::vec2 screenSize)
{
    m_workspaces.reserve(4);

    Amethyst::UIScope(m_window).tabBar(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, EDITOR_MENU_BAR_HEIGHT),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(EDITOR_MENU_BAR_HEIGHT + EDITOR_BOTTOM_BAR_HEIGHT)),
                },
            .style =
                {
                    .backgroundColor = Amethyst::Color3::fromHex(0x181818),
                },
            .tabBar =
                {
                    .mode = Amethyst::TabBarMode::INSIDE,
                    .barThickness = EDITOR_WORKSPACE_TAB_HEIGHT,
                },
        },
        [this, screenSize](Amethyst::TabBarScope &tabs) {
            m_workspaceTabBar = &tabs.component;

            auto setupTab = [this, screenSize](Amethyst::FrameScope &f) {
                Workspace ws;
                ws.container = &f.component;
                ws.container->setBaseStyleProperties({.backgroundColor = Amethyst::Color3::fromHex(0x181818)});

                ws.hotbar = ws.container->add<Amethyst::Frame>();
                ws.hotbar->setBaseProperties({
                    .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_HOTBAR_HEIGHT),
                });
                ws.hotbar->setBaseStyleProperties({
                    .backgroundColor = Amethyst::Color3::fromHex(0x303030),
                    .backgroundTransparency = 0.0f,
                    .borderPixelSize = 0.0f,
                });

                ws.dockingLayer = ws.container->add<Amethyst::DockingLayer>();
                ws.dockingLayer->setDisplayOrder(1);
                ws.dockingLayer->innerSpacing = 3.0f;
                ws.dockingLayer->absolutePosition = {0.0f, EDITOR_CONTENT_TOP + EDITOR_DOCK_SPACING};
                ws.dockingLayer->absoluteSize = {
                    screenSize.x,
                    screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING - EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING,
                };
                ws.dockingLayer->markDirty();

                m_workspaces.push_back(std::move(ws));
            };

            tabs.tab("Level Editor", setupTab);
            tabs.tab("Material Editor", setupTab);
            tabs.tab("Scripting", setupTab);
            tabs.tab("Animations", setupTab);
        });

    auto &levelEditor = m_workspaces[0];
    levelEditor.dockingLayer->name = "Editor Dock";
    levelEditor.dockingLayer->persistLayout = true;

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *outlinerTabBar = nullptr;
    Amethyst::TabBar *propertiesTabBar = nullptr;
    Amethyst::TabBar *contentBrowserTabBar = nullptr;

    Amethyst::DockScope(*levelEditor.dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.25f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { outlinerTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, 0.65f,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) {
                        b.split(
                            Amethyst::SplitAxis::VERTICAL, 0.5f,
                            [&](Amethyst::DockScope &bl) {
                                bl.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; });
                            },
                            [&](Amethyst::DockScope &br) {
                                br.panel([&](Amethyst::TabBarScope &tb) { contentBrowserTabBar = &tb.component; });
                            });
                    });
            });

    if (viewportTabBar != nullptr) viewportTabBar->addClass("panel-tab-bar");
    if (outlinerTabBar != nullptr) outlinerTabBar->addClass("panel-tab-bar");
    if (propertiesTabBar != nullptr) propertiesTabBar->addClass("panel-tab-bar");
    if (contentBrowserTabBar != nullptr) contentBrowserTabBar->addClass("panel-tab-bar");

    levelEditor.panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar));
    levelEditor.panels.push_back(std::make_unique<OutlinerPanel>(outlinerTabBar));
    levelEditor.panels.push_back(std::make_unique<PropertiesPanel>(propertiesTabBar));
    levelEditor.panels.push_back(std::make_unique<ContentBrowserPanel>(contentBrowserTabBar));

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                levelEditor.dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void AmethystLayer::beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.pNext = nullptr;
    colorAttachmentInfo.imageView = targetImageView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachmentInfo.resolveImageView = VK_NULL_HANDLE;
    colorAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color = {{1.0f, 0.0f, 1.0f, 1.0f}};

    VkImageMemoryBarrier toColorAttachment{};
    toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.image = swapChain->getImages()[m_currentImageIndex];
    toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColorAttachment.subresourceRange.baseMipLevel = 0;
    toColorAttachment.subresourceRange.levelCount = 1;
    toColorAttachment.subresourceRange.baseArrayLayer = 0;
    toColorAttachment.subresourceRange.layerCount = 1;
    toColorAttachment.srcAccessMask = 0;
    toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toColorAttachment);

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.pNext = nullptr;
    renderingInfo.flags = 0;
    renderingInfo.renderArea = {{0, 0}, {swapChain->getExtent().width, swapChain->getExtent().height}};
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBufferVk, &renderingInfo);
}

void AmethystLayer::endDynamicRendering(Rapture::CommandBuffer *commandBuffer)
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    vkCmdEndRendering(commandBufferVk);

    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = swapChain->getImages()[m_currentImageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;
    presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &presentBarrier);
}

void AmethystLayer::onResize()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    vulkanContext.waitIdle();

    for (auto &texId : m_viewportTextureIds) {
        if (texId.isValid()) {
            m_backend.unregisterTexture(texId);
        }
    }

    m_viewportTextureIds.clear();
    m_viewportTextureIds.resize(swapChain->getImageCount());

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    m_window.absoluteSize = screenSize;
    m_window.markDirty();

    for (auto &ws : m_workspaces) {
        if (ws.dockingLayer != nullptr) {
            ws.dockingLayer->absoluteSize = {screenSize.x, screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING -
                                                               EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING};
            ws.dockingLayer->markDirty();
        }
    }

    m_backend.onResize(screenSize);

    m_imageCount = swapChain->getImageCount();
}
