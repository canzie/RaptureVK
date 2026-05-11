#ifndef RAPTURE__RENDER_CONTEXT_H
#define RAPTURE__RENDER_CONTEXT_H

namespace Rapture {

class VulkanContext;
class BufferPoolManager;
class CommandPoolManager;
class DescriptorManager;

struct RenderContext {
    VulkanContext* vulkanContext = nullptr;
    BufferPoolManager* bufferPoolManager = nullptr;
    CommandPoolManager* commandPoolManager = nullptr;
    DescriptorManager* descriptorManager = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__RENDER_CONTEXT_H
