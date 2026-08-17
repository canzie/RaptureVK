#include "SkeletonInstanceManager.h"

#include "assets/skeletons/Skeleton.h"
#include "core/utils/Log.h"
#include "core/utils/rp_assert.h"
#include "gpu/descriptors/DescriptorSet.h"

#include <vector>

namespace Rapture {

static constexpr VkDeviceSize BONE_MATRIX_BYTES = sizeof(glm::mat4);
static constexpr VkDeviceSize SKELETON_ARENA_BONES = 1024 * BONE_MATRIX_BYTES;

SkeletonInstance::SkeletonInstance(VirtualStorageBuffer &buffer, uint32_t jointCount)
    : m_buffer(&buffer), m_allocation(buffer.allocate(jointCount * BONE_MATRIX_BYTES))
{
    RP_ASSERT(m_allocation.isValid(), "the skeleton arena has no room for {} more bones", jointCount);

    // a joint that has not moved off its bind pose skins by the identity, which is what an instance
    // nothing has posed yet holds
    std::vector<glm::mat4> restPose(jointCount, glm::mat4(1.0f));
    write(restPose);
}

SkeletonInstance::~SkeletonInstance()
{
    if (m_buffer != nullptr) {
        m_buffer->free(m_allocation);
    }
}

SkeletonInstance::SkeletonInstance(SkeletonInstance &&other) noexcept
    : m_buffer(other.m_buffer), m_allocation(std::move(other.m_allocation))
{
    other.m_buffer = nullptr;
}

SkeletonInstance &SkeletonInstance::operator=(SkeletonInstance &&other) noexcept
{
    if (this != &other) {
        if (m_buffer != nullptr) {
            m_buffer->free(m_allocation);
        }
        m_buffer = other.m_buffer;
        m_allocation = std::move(other.m_allocation);
        other.m_buffer = nullptr;
    }
    return *this;
}

uint32_t SkeletonInstance::getBoneOffset() const
{
    return static_cast<uint32_t>(m_allocation.getOffsetBytes() / BONE_MATRIX_BYTES);
}

uint32_t SkeletonInstance::getJointCount() const
{
    return static_cast<uint32_t>(m_allocation.getSizeBytes() / BONE_MATRIX_BYTES);
}

void SkeletonInstance::write(std::span<const glm::mat4> matrices)
{
    if (matrices.size() != getJointCount()) {
        RP_CORE_ERROR("wrote {} bone matrices to an instance holding {}", matrices.size(), getJointCount());
        return;
    }

    m_buffer->write(m_allocation, std::as_bytes(matrices));
}

SkeletonInstanceManager::SkeletonInstanceManager()
    : m_buffer(std::make_unique<VirtualStorageBuffer>(SKELETON_ARENA_BONES, BONE_MATRIX_BYTES,
                                                      DescriptorSetBindingLocation::SKELETON_MATRICES_SSBO))
{
    m_bindlessIndex = m_buffer->getDescriptorIndex();
}

SkeletonInstanceManager::~SkeletonInstanceManager() = default;

SkeletonInstance SkeletonInstanceManager::createSkeletonInstance(const AssetPtr<Skeleton> &skeleton)
{
    RP_ASSERT(skeleton && skeleton->getJointCount() > 0, "a skeleton instance needs a skeleton with joints");

    return SkeletonInstance(*m_buffer, skeleton->getJointCount());
}

} // namespace Rapture
