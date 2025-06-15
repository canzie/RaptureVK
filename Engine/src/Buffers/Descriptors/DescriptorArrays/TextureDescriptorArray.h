#pragma once

#include "Buffers/Descriptors/DescriptorArrayBase.h"
#include "Buffers/Descriptors/DescriptorArraySubAllocationBase.h"
#include "Buffers/Descriptors/DescriptorArrayTypes.h"
#include "Textures/Texture.h"

#include <memory>

namespace Rapture {

class TextureDescriptorSubAllocation : public DescriptorSubAllocationBase<Texture> {
public:
    TextureDescriptorSubAllocation(DescriptorArrayBase<Texture>* parent, uint32_t startIndex, uint32_t capacity, std::string name = "")
        : DescriptorSubAllocationBase<Texture>(parent, startIndex, capacity, name) {}
    
    ~TextureDescriptorSubAllocation();
};

class TextureDescriptorArray : public DescriptorArrayBase<Texture> {
public:
    TextureDescriptorArray(const DescriptorArrayConfig& config, VkDescriptorSet set);
    ~TextureDescriptorArray() = default;

    std::unique_ptr<DescriptorSubAllocationBase<Texture>> createSubAllocation(uint32_t capacity, std::string name = "") override;
    uint32_t allocate(std::shared_ptr<Texture> resource) override;
    void update(uint32_t index, std::shared_ptr<Texture> resource) override;
    void free(uint32_t index) override;

protected:
    void initializeSlotsWithDefault() override;
    std::shared_ptr<Texture> createDefaultResource() override;
};

}