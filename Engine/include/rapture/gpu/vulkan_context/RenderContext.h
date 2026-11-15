#ifndef RAPTURE__RENDER_CONTEXT_H
#define RAPTURE__RENDER_CONTEXT_H

namespace Rapture {

class VulkanContext;
class BufferPoolManager;
class CommandPoolManager;
class DescriptorManager;
class AccelerationStructureBuilder;

struct RenderContext {
    VulkanContext *vulkanContext = nullptr;
    BufferPoolManager *bufferPoolManager = nullptr;
    CommandPoolManager *commandPoolManager = nullptr;
    DescriptorManager *descriptorManager = nullptr;
    AccelerationStructureBuilder *accelerationStructureBuilder = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__RENDER_CONTEXT_H
