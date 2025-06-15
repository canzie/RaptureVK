#include "DescriptorArrayBase.h"
#include "DescriptorArrayTypes.h"
#include "Logging/Log.h"
#include "Textures/Texture.h"
#include "Buffers/Buffers.h"

namespace Rapture {

template<typename T>
DescriptorArrayBase<T>::DescriptorArrayBase(const DescriptorArrayConfig& config, VkDescriptorSet set)
    : m_type(config.getTypeVK()), m_capacity(config.capacity), m_name(config.name), m_bindingIndex(config.bindingIndex), m_set(set) {
    // Base initialization - derived classes will handle the rest
}

template<typename T>
DescriptorArrayBase<T>::~DescriptorArrayBase() {
    // Descriptor set resources are now managed by DescriptorArrayManager
    RP_CORE_INFO("Destroyed DescriptorArrayBase: {}", m_name);
}

// Explicit template instantiations for the types we use
template class DescriptorArrayBase<Texture>;
template class DescriptorArrayBase<Buffer>;

} // namespace Rapture 