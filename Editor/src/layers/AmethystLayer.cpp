#include "AmethystLayer.h"

#include "EditorState.h"
#include "ProjectLauncher.h"
#include "commands/EditorCommands.h"
#include "context/WindowManager.h"
#include "context/WorkspaceManager.h"
#include "undo/UndoHistory.h"
#include "workspaces/Workspace.h"
#include <rapture/app/Application.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/Timestep.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <rapture/gpu/swap_chains/SwapChain.h>
#include <rapture/input/Input.h>

#include <components/common.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/image_label.h>
#include <components/ui_scope.h>
#include <modules/color.h>
#include <modules/style.h>
#include <parsers/config/layout_config.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <string_view>

// the first font loaded is the default every unset font-family falls back to
static constexpr std::array<std::string_view, 3> FONT_FILES = {
    "Roboto-Regular.ttf",
    "Roboto-Bold.ttf",
    "IBMPlexMono-Regular.ttf",
};

static constexpr Amethyst::vec2 TOOLTIP_CURSOR_OFFSET = {14.0f, 22.0f};
static constexpr float PROJECT_NAME_WIDTH = 180.0f;
static constexpr float PROJECT_NAME_RIGHT_INSET = 140.0f;
static constexpr float PROJECT_NAME_VERTICAL_INSET = 8.0f;

static constexpr int32_t LAUNCHER_WINDOW_WIDTH = 640;
static constexpr int32_t LAUNCHER_WINDOW_HEIGHT = 520;

static constexpr std::string_view LAYOUT_CONFIG_FILE = "layout.conf";

static Rapture::ControlInput s_mapEditorCameraInput(const Rapture::Input &input)
{
    using namespace Rapture;
    ControlInput intent;
    intent.move.x = (input.isKeyPressed(KEY_D) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_A) ? 1.0f : 0.0f);
    intent.move.y = (input.isKeyPressed(KEY_SPACE) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_LEFT_SHIFT) ? 1.0f : 0.0f);
    intent.move.z = (input.isKeyPressed(KEY_W) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_S) ? 1.0f : 0.0f);
    intent.look = input.mouseDelta();
    intent.zoom = input.scrollDelta();
    intent.orbit = input.isMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    intent.pan = intent.orbit && input.isKeyPressed(KEY_LEFT_SHIFT);
    intent.releaseControl = input.isKeyPressed(KEY_ESCAPE);
    return intent;
}

VkDescriptorPool AmethystLayer::createUiDescriptorPool()
{
    auto &vulkanContext = Rapture::Application::getInstance().getVulkanContext();

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_TEXTURES},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &poolInfo, nullptr, &pool);
    return pool;
}

AmethystLayer::AmethystLayer()
{
    Amethyst::Log::Init();

    auto &app = Rapture::Application::getInstance();

    auto themePath = Rapture::EnginePaths::assetDirectory() / "themes/theme.ams";
    Amethyst::Style::load(themePath);

    m_mainSwapchainRecreatedConn = app.getMainWindow().getSwapChain()->onRecreated.connect(
        [this]() { onResize(*Rapture::Application::getInstance().getMainWindow().getSwapChain()); });

    m_config = EditorConfig::load();
    Amethyst::LayoutConfig::instance().loadFromFile(LAYOUT_CONFIG_FILE);
}

AmethystLayer::~AmethystLayer()
{
    if (m_workspaceManager != nullptr) {
        m_workspaceManager->saveDockLayouts();
    }

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    // panels hand their textures back to the backend as they go, so they go before it shuts down
    m_activeWorkspaceChangedConn.disconnect();
    m_bottomBar.reset();
    m_workspaceManager.reset();
    m_windowManager.reset();

    m_backend.shutdown();
    vkDestroyDescriptorPool(vulkanContext.getLogicalDevice(), m_uiDescriptorPool, nullptr);
    Amethyst::Log::Shutdown();

    RP_INFO("---Closing AmethystLayer---");
}

void AmethystLayer::onAttach()
{
    RP_INFO("Attaching AmethystLayer...");

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto &window = app.getWindowContext();
    auto swapChain = app.getMainWindow().getSwapChain();

    m_input = std::make_unique<Rapture::Input>(&window);
    m_uiDescriptorPool = createUiDescriptorPool();

    auto amethystShaders = Rapture::EnginePaths::assetDirectory() / "amethyst/spirv";
    auto vertShaderPath = (amethystShaders / "ui.vs.spv").string();
    auto fragShaderPath = (amethystShaders / "ui.fs.spv").string();

    Amethyst::AmVulkanInitInfo initInfo{};
    initInfo.device = vulkanContext.getLogicalDevice();
    initInfo.instance = vulkanContext.getInstance();
    initInfo.physicalDevice = vulkanContext.getPhysicalDevice();
    initInfo.queue = vulkanContext.getVendorQueue()->getQueueVk();
    initInfo.queueFamiliy = vulkanContext.getGraphicsQueueIndex();
    initInfo.pool = m_uiDescriptorPool;
    initInfo.minImageCount = swapChain->getImageCount();
    initInfo.imageCount = swapChain->getImageCount();
    initInfo.colorFormat = swapChain->getImageFormat();
    initInfo.extent = swapChain->getExtent();
    initInfo.vertexShaderPath = vertShaderPath.c_str();
    initInfo.fragmentShaderPath = fragShaderPath.c_str();

    Amethyst::AmGlfwInitInfo glfwInfo{};
    glfwInfo.window = static_cast<GLFWwindow *>(window.getNativeWindowContext());
    glfwInfo.uiWindow = &m_window;

    m_backend.init(initInfo, glfwInfo);

    for (std::string_view fontFile : FONT_FILES) {
        auto fontPath = Rapture::EnginePaths::assetDirectory() / "fonts" / fontFile;
        if (!m_amCtx.loadFont(fontPath.string()).isValid()) {
            RP_WARN("Failed to load font from: {}", fontPath.string());
        }
    }

    m_amCtx.init(m_backend);

    m_window.setFramebufferSize({static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)});
    m_window.absoluteRotation = 0.0f;

    m_backgroundFrame = m_window.add<Amethyst::Frame>();
    m_backgroundFrame->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
        .zIndex = 0,
    });
    m_backgroundFrame->setBaseStyleProperties({.backgroundColor = Amethyst::Color3::fromHex(0x181818)});

    m_windowManager =
        std::make_unique<WindowManager>(m_backend, m_window, [this](Amethyst::Window &window, Rapture::RenderWindow &renderWindow) {
            drawSecondaryWindow(window, renderWindow);
        });

    if (!app.hasProject()) {
        m_startupLauncher = std::make_unique<ProjectLauncher>(m_window);
        wireLauncher(*m_startupLauncher);
        return;
    }

    m_window.getTooltips().cursorOffset = TOOLTIP_CURSOR_OFFSET;
    setupEditorContext();
    setupMenuBar();

    m_bottomBar = std::make_unique<BottomBar>(&m_window, m_editorContext);
    setupWorkspaces();
}

void AmethystLayer::setupEditorContext()
{
    m_editorContext = {
        .amethyst = &m_amCtx,
        .windows = m_windowManager.get(),
        .workspaces = nullptr,
        .textures = &m_textures,
        .shortcuts = &m_shortcutRegistry,
        .config = &m_config,
        .project = &Rapture::Application::getInstance().getProject(),
        .cameraInput = &m_cameraInput,
        .input = m_input.get(),
        .sceneObjectClipboard = &m_sceneObjectClipboard,
        .showAssetInContentBrowser =
            [this](Rapture::AssetHandle handle) {
                if (m_bottomBar != nullptr) {
                    m_bottomBar->showAssetInContentBrowser(handle);
                }
            },
    };

    m_workspaceManager = std::make_unique<WorkspaceManager>(m_editorContext);
    m_editorContext.workspaces = m_workspaceManager.get();

    m_commands = std::make_unique<EditorCommands>(m_config, *m_workspaceManager, Rapture::Application::getInstance().getMainWindow());
    m_editorContext.commands = m_commands.get();
}

void AmethystLayer::wireLauncher(ProjectLauncher &launcher)
{
    launcher.onOpenProject = [this](const std::filesystem::path &projectPath) { launchProject(projectPath); };
    launcher.onCreateProject = [this](std::string_view name) { createProject(name); };
    launcher.onBrowseForProject = [this]() {
        m_windowManager->openFileDialog(FILE_DIALOG_OPEN, [this](const std::filesystem::path &path) { launchProject(path); });
    };
}

void AmethystLayer::openLauncherWindow()
{
    m_windowManager->openWindow(LAUNCHER_WINDOW_WIDTH, LAUNCHER_WINDOW_HEIGHT, "Rapture Projects",
                                [this](Amethyst::Window &window, const std::function<void()> &close) {
                                    (void)close;
                                    auto launcher = std::make_shared<ProjectLauncher>(window);
                                    wireLauncher(*launcher);
                                    return launcher;
                                });
}

void AmethystLayer::createProject(std::string_view name)
{
    auto &app = Rapture::Application::getInstance();
    std::filesystem::path directory = Rapture::EnginePaths::executableDirectory() / "projects" / name;

    std::filesystem::path projectPath = app.createProject(directory, name);
    if (projectPath.empty()) {
        return;
    }

    launchProject(projectPath);
}

void AmethystLayer::launchProject(const std::filesystem::path &projectPath)
{
    m_config.addRecentProject(projectPath);
    m_config.setAutoLaunchProject(projectPath);
    m_config.save();

    // the editor is built around an open project, so it restarts into this one
    Rapture::Application::getInstance().requestRelaunch(projectPath);
}

void AmethystLayer::onDetach()
{
    RP_INFO("Detaching AmethystLayer...");

    if (m_editorCameraHoldsCursor) {
        m_input->setCursorMode(Rapture::CursorMode::NORMAL);
        m_editorCameraHoldsCursor = false;
    }

    if (m_workspaceManager == nullptr) {
        return;
    }

    m_workspaceManager->saveDockLayouts();
    storeEditorState();
}

void AmethystLayer::storeEditorState()
{
    EditorState state = m_workspaceManager->captureState();

    auto &project = Rapture::Application::getInstance().getProject();
    state.store(project);
    project.saveProject(project.getProjectFilePath());
}

void AmethystLayer::onUpdate(float dt)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &mainWindow = app.getMainWindow();

    // before the frame is drawn, so what a call changes is in the frame a screenshot captures
    if (m_commands != nullptr) {
        m_commands->onUpdate();
    }

    auto frame = mainWindow.beginFrame();
    if (!frame.acquired) {
        return;
    }

    updateShortcuts();
    readEditorCameraInput();

    if (m_workspaceManager != nullptr) {
        m_workspaceManager->onUpdate(dt);
    }
    if (m_bottomBar != nullptr) {
        m_bottomBar->onUpdate();
    }

    applyEditorCameraCursor();

    m_window.tick(dt);

    VkCommandBuffer cmd = frame.commandBuffer->getCommandBufferVk();

    m_amCtx.draw(m_window);
    m_amCtx.syncShared(static_cast<void *>(cmd));
    m_amCtx.syncWindow(static_cast<void *>(cmd), m_window);

    beginDynamicRendering(frame.commandBuffer, frame.imageView, frame.imageIndex, *mainWindow.getSwapChain());

    m_backend.record(cmd, m_amCtx.getDrawList(m_window), mainWindow.getSwapChain()->getExtent());

    endDynamicRendering(frame.commandBuffer, frame.imageIndex, *mainWindow.getSwapChain());

    mainWindow.endFrame();
}

static void s_saveProject()
{
    auto &project = Rapture::Application::getInstance().getProject();
    project.saveProject(project.getProjectFilePath());
}

void AmethystLayer::setupMenuBar()
{
    const float menuBarHeight = m_config.getFloat(EDITOR_CONFIG_MENU_BAR_HEIGHT);

    Amethyst::UIScope(m_window).menuBar(
        {
            .classes = {"main-menu-bar"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, menuBarHeight),
                },
            .style =
                {
                    .backgroundTransparency = 0.0f,
                    .borderPixelSize = 0.0f,
                },
        },
        [this](Amethyst::MenuBarScope &mb) {
            m_menuBar = &mb.component;
            m_shortcutRegistry.registerShortcut(EDITOR_COMMAND_EXIT, {Rapture::KEY_Q, SHORTCUT_MOD_CONTROL}, m_backgroundFrame,
                                                [] { Rapture::Application::getInstance().close(); });
            mb.menuItem("File", [this](Amethyst::DropdownScope &d) {
                d.action("New Scene", [] {});
                d.action("Open Scene", [] {});
                d.action("Save Scene", [] {});
                d.separator();
                d.action("New Project", [this] { openLauncherWindow(); });
                d.action("Open Project", [this] { openLauncherWindow(); });
                d.action("Save Project", [] { s_saveProject(); });
                d.separator();
                d.action("Exit", [] { Rapture::Application::getInstance().close(); });
            });
            mb.menuItem("Edit", [this](Amethyst::DropdownScope &d) {
                d.action("Undo", [this] {
                    if (UndoHistory *history = activeWorkspaceUndoHistory()) {
                        history->undo();
                    }
                });
                d.action("Redo", [this] {
                    if (UndoHistory *history = activeWorkspaceUndoHistory()) {
                        history->redo();
                    }
                });
                d.separator();
                d.action("Cut", [] {});
                d.action("Copy", [] {});
                d.action("Paste", [] {});
                d.separator();
                d.action("Editor Preferences", [] {});
            });
            mb.menuItem("Window", [this](Amethyst::DropdownScope &d) {
                d.action("Viewport", [] {});
                d.action("Outliner", [] {});
                d.action("Properties", [] {});
                d.action("Content Browser", [] {});
                d.separator();
                d.action("New Window", [this] { openDemoWindow(); });
            });
            mb.menuItem("Help", [](Amethyst::DropdownScope &d) {
                d.action("Documentation", [] {});
                d.action("About", [] {});
            });
        });

    // a sibling of the bar rather than a child, since a MenuBar flows every child it has as an entry
    Amethyst::UIScope(m_window).textLabel({
        .classes = {"main-menu-bar-project"},
        .base =
            {
                .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(1.0f, -PROJECT_NAME_RIGHT_INSET, 0.0f, menuBarHeight * 0.5f),
                .size = Amethyst::UDim2(0.0f, PROJECT_NAME_WIDTH, 0.0f, menuBarHeight - PROJECT_NAME_VERTICAL_INSET),
                .zIndex = 2,
            },
        .label = Rapture::Application::getInstance().getProject().getProjectName(),
    });
}

void AmethystLayer::setupWorkspaces(void)
{
    const float menuBarHeight = m_config.getFloat(EDITOR_CONFIG_MENU_BAR_HEIGHT);
    const float bottomBarHeight = m_config.getFloat(EDITOR_CONFIG_BOTTOM_BAR_HEIGHT);

    Amethyst::UIScope(m_window).tabBar(
        {
            .classes = {"workspace", "workspace-tab"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, menuBarHeight),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(menuBarHeight + bottomBarHeight)),
                },
            .tabBar =
                {
                    .mode = Amethyst::TabBarMode::INSIDE,
                    .barThickness = m_config.getFloat(EDITOR_CONFIG_WORKSPACE_TAB_HEIGHT),
                    .tabWidth = m_config.getFloat(EDITOR_CONFIG_WORKSPACE_TAB_WIDTH),
                },
        },
        [this](Amethyst::TabBarScope &tabs) { m_workspaceTabBar = &tabs.component; });

    m_activeWorkspaceChangedConn = m_workspaceManager->onActiveWorkspaceChanged.connect(
        [this](Workspace *workspace) { m_bottomBar->setCurrentWorkspace(workspace); });

    m_workspaceManager->attach(*m_workspaceTabBar);
    placeWorkspaceContent(m_window.absoluteSize);
    m_workspaceManager->restore(EditorState::load(*m_editorContext.project));
}

void AmethystLayer::placeWorkspaceContent(glm::vec2 screenSize)
{
    const float contentTop =
        m_config.getFloat(EDITOR_CONFIG_MENU_BAR_HEIGHT) + m_config.getFloat(EDITOR_CONFIG_WORKSPACE_TAB_HEIGHT);
    const float bottomBarHeight = m_config.getFloat(EDITOR_CONFIG_BOTTOM_BAR_HEIGHT);

    m_workspaceManager->setContentArea({0.0f, contentTop}, {screenSize.x, screenSize.y - contentTop - bottomBarHeight});
}

void AmethystLayer::beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView, uint32_t imageIndex,
                                          const Rapture::SwapChain &swapChain)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

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
    toColorAttachment.image = swapChain.getImages()[imageIndex];
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
    renderingInfo.renderArea = {{0, 0}, {swapChain.getExtent().width, swapChain.getExtent().height}};
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBufferVk, &renderingInfo);
}

void AmethystLayer::endDynamicRendering(Rapture::CommandBuffer *commandBuffer, uint32_t imageIndex,
                                        const Rapture::SwapChain &swapChain)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    vkCmdEndRendering(commandBufferVk);

    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = swapChain.getImages()[imageIndex];
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

void AmethystLayer::onResize(const Rapture::SwapChain &swapChain)
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    vulkanContext.waitIdle();

    m_window.setFramebufferSize({static_cast<float>(swapChain.getExtent().width), static_cast<float>(swapChain.getExtent().height)});

    if (m_workspaceManager != nullptr) {
        placeWorkspaceContent(m_window.absoluteSize);
    }
}

UndoHistory *AmethystLayer::activeWorkspaceUndoHistory() const
{
    if (m_workspaceManager == nullptr || m_workspaceManager->activeWorkspace() == nullptr) {
        return nullptr;
    }
    return m_workspaceManager->activeWorkspace()->getContext().undoHistory;
}

void AmethystLayer::updateShortcuts()
{
    auto &windowContext = Rapture::Application::getInstance().getWindowContext();

    // the cursor comes back in window pixels while the UI is placed in logical ones
    const glm::vec2 cursor =
        windowContext.getCursorPosition() * windowContext.getContentScale() / m_window.getLogicalToPhysicalScale();
    m_shortcutRegistry.onUpdate(*m_input, cursor);
}

void AmethystLayer::readEditorCameraInput()
{
    m_input->onUpdate();
    m_cameraInput.intent = s_mapEditorCameraInput(*m_input);
    m_cameraInput.cursorCaptured = false;
}

void AmethystLayer::applyEditorCameraCursor()
{
    // only on a change, so a play session holding the cursor is left alone
    if (m_cameraInput.cursorCaptured == m_editorCameraHoldsCursor) {
        return;
    }

    m_editorCameraHoldsCursor = m_cameraInput.cursorCaptured;
    m_input->setCursorMode(m_editorCameraHoldsCursor ? Rapture::CursorMode::DISABLED : Rapture::CursorMode::NORMAL);
}

void AmethystLayer::openDemoWindow(void)
{
    m_windowManager->openWindow(480, 270, "Demo Window", [](Amethyst::Window &win, const std::function<void()> &close) {
        (void)close;

        auto *background = win.add<Amethyst::Frame>();
        background->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
            .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
            .zIndex = 0,
        });
        background->addClass("background-primary");

        Amethyst::UIScope(win).textLabel({
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
            .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = "Hello from a second window!",
        });
        return std::shared_ptr<void>{};
    });
}

void AmethystLayer::drawSecondaryWindow(Amethyst::Window &window, Rapture::RenderWindow &renderWindow)
{
    auto frame = renderWindow.beginFrame();
    if (!frame.acquired) {
        return;
    }

    window.tick(Rapture::Timestep::deltaTime());

    VkCommandBuffer cmd = frame.commandBuffer->getCommandBufferVk();

    m_amCtx.draw(window);
    m_amCtx.syncWindow(static_cast<void *>(cmd), window);

    beginDynamicRendering(frame.commandBuffer, frame.imageView, frame.imageIndex, *renderWindow.getSwapChain());

    m_backend.record(cmd, m_amCtx.getDrawList(window), renderWindow.getSwapChain()->getExtent());

    endDynamicRendering(frame.commandBuffer, frame.imageIndex, *renderWindow.getSwapChain());

    renderWindow.endFrame();
}
