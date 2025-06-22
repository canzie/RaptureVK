#pragma once

#include "Buffers/Descriptors/DescriptorArrayBase.h"
#include "Buffers/Descriptors/DescriptorArraySubAllocationBase.h"
#include "Buffers/Descriptors/DescriptorArrayTypes.h"
#include "Buffers/Buffers.h"

#include <memory>

namespace Rapture {

class BufferDescriptorSubAllocation : public DescriptorSubAllocationBase<Buffer> {
public:
    BufferDescriptorSubAllocation(DescriptorArrayBase<Buffer>* parent, uint32_t startIndex, uint32_t capacity, std::string name = "")
        : DescriptorSubAllocationBase<Buffer>(parent, startIndex, capacity, name) {}
    
    ~BufferDescriptorSubAllocation();
};

class BufferDescriptorArray : public DescriptorArrayBase<Buffer> {
public:
    BufferDescriptorArray(const DescriptorArrayConfig& config, VkDescriptorSet set);
    ~BufferDescriptorArray() = default;

    std::unique_ptr<DescriptorSubAllocationBase<Buffer>> createSubAllocation(uint32_t capacity, std::string name = "") override;
    uint32_t allocate(std::shared_ptr<Buffer> resource) override;
    void update(uint32_t index, std::shared_ptr<Buffer> resource) override;
    void free(uint32_t index) override;

protected:
    void initializeSlotsWithDefault() override;
    std::shared_ptr<Buffer> createDefaultResource() override;
};

}