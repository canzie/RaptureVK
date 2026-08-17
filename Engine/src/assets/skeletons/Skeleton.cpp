#include "Skeleton.h"

#include "core/utils/Log.h"

#include <cstring>

namespace Rapture {

static constexpr uint32_t SKELETON_BLOB_MAGIC = 0x4C454B53; // "SKEL"
static constexpr uint32_t SKELETON_BLOB_VERSION = 1;

struct SkeletonBlobHeader {
    uint32_t magic = SKELETON_BLOB_MAGIC;
    uint32_t version = SKELETON_BLOB_VERSION;
    uint32_t jointCount = 0;
    uint32_t parentsOffset = 0;
    uint32_t restPoseOffset = 0;
    uint32_t namesOffset = 0;
    uint32_t reserved[10] = {}; // pad to 64 bytes, consume for backward-compatible additions
};

static_assert(sizeof(SkeletonBlobHeader) == 64, "SkeletonBlobHeader must stay 64 bytes");

Skeleton::Skeleton(std::vector<JointIndex> parents, std::vector<std::string> names, Pose restPose)
    : m_parents(std::move(parents)), m_names(std::move(names)), m_restPose(std::move(restPose))
{
}

Skeleton::JointIndex Skeleton::addJoint(std::string name, JointIndex parent, const JointTransform &restTransform)
{
    if (parent != INVALID_JOINT_INDEX && parent >= getJointCount()) {
        RP_CORE_ERROR("cannot hang joint '{}' from joint {}, which does not exist", name, parent);
        return INVALID_JOINT_INDEX;
    }

    // appending keeps a joint after its parent, since the parent already exists
    JointIndex joint = getJointCount();
    m_parents.push_back(parent);
    m_names.push_back(std::move(name));
    m_restPose.joints.push_back(restTransform);

    onChanged.fire();
    return joint;
}

bool Skeleton::removeJoint(JointIndex joint)
{
    if (joint >= getJointCount()) {
        RP_CORE_ERROR("cannot remove joint {}, which does not exist", joint);
        return false;
    }

    JointIndex grandparent = m_parents[joint];

    m_parents.erase(m_parents.begin() + joint);
    m_names.erase(m_names.begin() + joint);
    m_restPose.joints.erase(m_restPose.joints.begin() + joint);

    for (JointIndex &parent : m_parents) {
        if (parent == joint) {
            parent = grandparent;
        } else if (parent != INVALID_JOINT_INDEX && parent > joint) {
            parent--;
        }
    }

    onChanged.fire();
    return true;
}

bool Skeleton::setParent(JointIndex joint, JointIndex parent)
{
    if (joint >= getJointCount()) {
        RP_CORE_ERROR("cannot reparent joint {}, which does not exist", joint);
        return false;
    }
    if (parent != INVALID_JOINT_INDEX && parent >= joint) {
        RP_CORE_ERROR("cannot hang joint {} from joint {}, which does not precede it", joint, parent);
        return false;
    }

    m_parents[joint] = parent;
    onChanged.fire();
    return true;
}

void Skeleton::setName(JointIndex joint, std::string name)
{
    if (joint >= getJointCount()) {
        RP_CORE_ERROR("cannot rename joint {}, which does not exist", joint);
        return;
    }

    m_names[joint] = std::move(name);
    onChanged.fire();
}

void Skeleton::setRestTransform(JointIndex joint, const JointTransform &restTransform)
{
    if (joint >= getJointCount()) {
        RP_CORE_ERROR("cannot set the rest transform of joint {}, which does not exist", joint);
        return;
    }

    m_restPose.joints[joint] = restTransform;
    onChanged.fire();
}

Skeleton::JointIndex Skeleton::findJoint(std::string_view name) const
{
    for (JointIndex joint = 0; joint < m_names.size(); ++joint) {
        if (m_names[joint] == name) {
            return joint;
        }
    }
    return INVALID_JOINT_INDEX;
}

std::vector<uint8_t> Skeleton::serialize() const
{
    uint32_t jointCount = getJointCount();

    uint32_t nameBytes = 0;
    for (const std::string &name : m_names) {
        nameBytes += static_cast<uint32_t>(sizeof(uint32_t) + name.size());
    }

    SkeletonBlobHeader header;
    header.jointCount = jointCount;
    header.parentsOffset = sizeof(SkeletonBlobHeader);
    header.restPoseOffset = header.parentsOffset + jointCount * static_cast<uint32_t>(sizeof(JointIndex));
    header.namesOffset = header.restPoseOffset + jointCount * static_cast<uint32_t>(sizeof(JointTransform));

    std::vector<uint8_t> blob(header.namesOffset + nameBytes);
    std::memcpy(blob.data(), &header, sizeof(SkeletonBlobHeader));

    if (jointCount > 0) {
        std::memcpy(blob.data() + header.parentsOffset, m_parents.data(), jointCount * sizeof(JointIndex));
        std::memcpy(blob.data() + header.restPoseOffset, m_restPose.joints.data(), jointCount * sizeof(JointTransform));
    }

    size_t offset = header.namesOffset;
    for (const std::string &name : m_names) {
        uint32_t length = static_cast<uint32_t>(name.size());
        std::memcpy(blob.data() + offset, &length, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        std::memcpy(blob.data() + offset, name.data(), length);
        offset += length;
    }

    return blob;
}

std::unique_ptr<Skeleton> Skeleton::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(SkeletonBlobHeader)) {
        RP_CORE_ERROR("skeleton blob is {} bytes, too small to hold a header", blob.size());
        return nullptr;
    }

    SkeletonBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(SkeletonBlobHeader));

    if (header.magic != SKELETON_BLOB_MAGIC) {
        RP_CORE_ERROR("skeleton blob has magic {:#x}, which is not a skeleton", header.magic);
        return nullptr;
    }
    if (header.version != SKELETON_BLOB_VERSION) {
        RP_CORE_ERROR("skeleton blob is version {}, this build reads version {}", header.version, SKELETON_BLOB_VERSION);
        return nullptr;
    }
    if (header.namesOffset > blob.size()) {
        RP_CORE_ERROR("skeleton blob names start past its end");
        return nullptr;
    }

    uint32_t jointCount = header.jointCount;

    std::vector<JointIndex> parents(jointCount);
    Pose restPose;
    restPose.joints.resize(jointCount);

    if (jointCount > 0) {
        std::memcpy(parents.data(), blob.data() + header.parentsOffset, jointCount * sizeof(JointIndex));
        std::memcpy(restPose.joints.data(), blob.data() + header.restPoseOffset, jointCount * sizeof(JointTransform));
    }

    std::vector<std::string> names(jointCount);
    size_t offset = header.namesOffset;
    for (uint32_t joint = 0; joint < jointCount; ++joint) {
        if (offset + sizeof(uint32_t) > blob.size()) {
            RP_CORE_ERROR("skeleton blob ends before the name of joint {}", joint);
            return nullptr;
        }
        uint32_t length = 0;
        std::memcpy(&length, blob.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + length > blob.size()) {
            RP_CORE_ERROR("skeleton blob ends inside the name of joint {}", joint);
            return nullptr;
        }
        names[joint].assign(reinterpret_cast<const char *>(blob.data() + offset), length);
        offset += length;
    }

    return std::make_unique<Skeleton>(std::move(parents), std::move(names), std::move(restPose));
}

} // namespace Rapture
