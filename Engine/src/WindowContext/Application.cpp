#include "Application.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Loaders/glTF2.0/glTFLoader.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Renderer/ForwardRenderer/ForwardRenderer.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/BindlessDescriptorManager.h"

#include "Utils/Timestep.h"

namespace Rapture {

Application *Application::s_instance = nullptr;

Application::Application(int width, int height, const char *title)
    : m_running(true), m_isMinimized(false)
// m_vulkanContext is not initialized here yet
{
  if (s_instance) {
    RP_CORE_ERROR("Application already exists!");
    // Or throw an exception
    return;
  }
  s_instance = this; // Set s_instance HERE

  RP_CORE_INFO("Creating window...");
  m_window = std::unique_ptr<WindowContext>(
      WindowContext::createWindow(width, height, title));

  RP_CORE_INFO("Creating Vulkan context...");
  m_vulkanContext =
      std::unique_ptr<VulkanContext>(new VulkanContext(m_window.get()));

  m_vulkanContext->createRecourses(m_window.get());

  TracyProfiler::init();

  // Initialize project - this will setup default world and scene
  m_project = std::make_unique<Project>();
  auto working_dir = std::filesystem::current_path();
  auto root_dir = working_dir;

  // Try to find the project root by looking for Engine folder
  const int max_steps = 4;
  int steps = 0;
  while (steps < max_steps) {
    // Check if Engine directory exists in current path
    if (std::filesystem::exists(root_dir / "Engine") &&
        std::filesystem::exists(root_dir / "build")) {
      break;
    }
    // Go up one directory
    auto parent = root_dir.parent_path();
    if (parent == root_dir) { // We've hit the root
      break;
    }
    root_dir = parent;
    steps++;
  }

  m_project->setProjectRootDirectory(root_dir);
  m_project->setProjectShaderDirectory(root_dir / "Engine/assets/shaders/");

  CommandPoolManager::init();

  AssetManager::init();
  MaterialManager::init();

  BindlessDescriptorArrayConfig config;
  config.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  config.capacity = 512;
  config.name = "GlobalBindlessTexturePool";
  config.setBindingIndex = 3; // set=3
  config.bindingIndex = 0;    // binding=0

  BindlessDescriptorManager::init({config});

  ForwardRenderer::init();
  DeferredRenderer::init();

  ModelLoadersCache::init();

  ApplicationEvents::onWindowClose().addListener(
      [this]() { m_running = false; });

  ApplicationEvents::onWindowFocus().addListener(
      [this]() { RP_CORE_INFO("Window focused"); });

  ApplicationEvents::onWindowLostFocus().addListener(
      [this]() { RP_CORE_INFO("Window lost focus"); });

  ApplicationEvents::onWindowResize().addListener(
      [this](unsigned int width, unsigned int height) {
        RP_CORE_INFO("Window resized to {}x{}", width, height);
      });

  RP_CORE_INFO("========== Application created ==========");
}

Application::~Application() {

  m_vulkanContext->waitIdle();

  TracyProfiler::shutdown();

  ModelLoadersCache::clear();
  m_project.reset();

  ForwardRenderer::shutdown();
  DeferredRenderer::shutdown();
  BindlessDescriptorManager::shutdown();

  m_layerStack.clear();

  MaterialManager::shutdown();
  AssetManager::shutdown();

  CommandPoolManager::shutdown();

  // Shutdown the event system and clear all listeners
  EventRegistry::getInstance().shutdown();

  RP_CORE_INFO("Application shutting down...");
}

void Application::run() {

  while (m_running) {
    TracyProfiler::beginFrame();

    Timestep::onUpdate();

    for (auto layer : m_layerStack) {
      layer->onUpdate(Timestep::deltaTime());
    }

    m_window->onUpdate();

    TracyProfiler::endFrame();
  }

  m_vulkanContext->waitIdle();
}

void Application::pushLayer(Layer *layer) {

  m_layerStack.pushLayer(layer);
  layer->onAttach();
}

void Application::pushOverlay(Layer *overlay) {

  m_layerStack.pushOverlay(overlay);
  overlay->onAttach();
}

} // namespace Rapture
