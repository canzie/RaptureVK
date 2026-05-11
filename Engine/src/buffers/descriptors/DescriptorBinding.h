/*

class DescriptorBinding;
    - a reference/ptr to the set object it belongs to
    - a type (array, single descriptor) -> image and ssbo arrays should go trough the descriptorsetarray system only ubos are
allowed to be arrays here
    - a method to add/update the data at its location (if it is an array also need the index)
    - the add method will return an index into the array if it is an array, otherwise something like 0
    - keep a vector of bools to track the current allocations, will be used when adding a new binding
    - a size

*/

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

class DescriptorSet;
class UniformBuffer;
class Buffer;
class Texture;
class TLAS;
enum class TextureViewType : uint8_t;

// TODO Add some sort of checking/verification for making sure that when adding a ubo to an array it is the same type
//      as the current one

template <typename T> class DescriptorBinding {

  public:
    DescriptorBinding(DescriptorSet *set, uint32_t binding, VkDescriptorType type, uint32_t size = 1);
    ~DescriptorBinding();

    // returns an index
    // =0 if the binding is not an array or the first element
    virtual uint32_t add(T &resource) = 0;

    // only expects a resource if the binding is an array
    virtual void update(T &resource, uint32_t index = 0) = 0;

    void free(uint32_t index = 0);

    uint32_t getSize() const { return m_size; }

  protected:
    void resize(uint32_t newSize);
    uint32_t findFreeIndex();
    void fillEmpty(); // fill the descriptor with empty data to prevent invalid reads

  protected:
    DescriptorSet *m_set; // parent set
    uint32_t m_binding;   // binding number
    uint32_t m_size;      // number of elements, NOT bytes
    bool m_isArray = false;
    VkDescriptorType m_type;
    // mask of the current allocations
    // only relevant when m_isArray is true
    std::vector<bool> m_isAllocated; // NOTE: Could use a weakptr here to help with resizing/changes
};

class DescriptorBindingUniformBuffer : public DescriptorBinding<UniformBuffer> {
  public:
    DescriptorBindingUniformBuffer(DescriptorSet *set, uint32_t binding, uint32_t size = 1);
    virtual uint32_t add(UniformBuffer &resource) override;
    virtual void update(UniformBuffer &resource, uint32_t index = 0) override;
};

class DescriptorBindingTexture : public DescriptorBinding<Texture> {
  public:
    DescriptorBindingTexture(DescriptorSet *set, uint32_t binding, TextureViewType viewType, bool isStorageImage,
                             uint32_t size = 1);
    virtual uint32_t add(Texture &resource) override;
    virtual void update(Texture &resource, uint32_t index = 0) override;

  private:
    void fillAllSlotsWithPlaceholder();

    TextureViewType m_viewType;
    bool m_isStorageImage;
};

class DescriptorBindingTLAS : public DescriptorBinding<TLAS> {
  public:
    DescriptorBindingTLAS(DescriptorSet *set, uint32_t binding, uint32_t size = 1);
    virtual uint32_t add(TLAS &resource) override;
    virtual void update(TLAS &resource, uint32_t index = 0) override;
};

class DescriptorBindingSSBO : public DescriptorBinding<Buffer> {
  public:
    DescriptorBindingSSBO(DescriptorSet *set, uint32_t binding, uint32_t size = 1);
    virtual uint32_t add(Buffer &resource) override;
    virtual void update(Buffer &resource, uint32_t index = 0) override;
};

} // namespace Rapture