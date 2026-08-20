#include "ASkeleton.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"

#include <algorithm>
#include <cstring>

namespace Rapture {

static constexpr uint32_t SKELETON_ASSET_MAGIC = Asset_fourCC("SKEL");

static constexpr uint16_t SKELETON_ASSET_VERSION_MAJOR = 2;
static constexpr uint16_t SKELETON_ASSET_VERSION_MINOR = 0;
static constexpr uint32_t SKELETON_ASSET_VERSION =
    (static_cast<uint32_t>(SKELETON_ASSET_VERSION_MAJOR) << 16) | SKELETON_ASSET_VERSION_MINOR;

struct ASkeletonBlobHeader {
    uint32_t magic = SKELETON_ASSET_MAGIC;
    uint32_t version = SKELETON_ASSET_VERSION;
    uint32_t previewMeshCount = 0;
    uint32_t previewMeshesOffset = 0;
    uint32_t skeletonOffset = 0;
    uint32_t reserved[3] = {}; // pad to 32 bytes, consume for backward-compatible additions
};

static_assert(sizeof(ASkeletonBlobHeader) == 32, "skeleton asset blob header is a fixed 32-byte directory");

ASkeleton::ASkeleton(std::unique_ptr<Skeleton> skeleton, std::vector<AssetHandle> previewMeshes)
    : m_skeleton(std::move(skeleton)), m_previewMeshes(std::move(previewMeshes))
{
    RP_ASSERT(m_skeleton != nullptr, "a skeleton asset has to hold a skeleton");
}

bool ASkeleton::addPreviewMesh(AssetHandle mesh)
{
    if (std::find(m_previewMeshes.begin(), m_previewMeshes.end(), mesh) != m_previewMeshes.end()) {
        return false;
    }

    const AssetMetadata &metadata = AssetManager::getAssetMetadata(mesh);
    if (metadata.assetType != ASSET_SKELETAL_MESH) {
        RP_CORE_ERROR("{} is not a skeletal mesh, so it cannot show a skeleton", mesh);
        return false;
    }

    m_previewMeshes.push_back(mesh);
    return true;
}

bool ASkeleton::removePreviewMesh(AssetHandle mesh)
{
    auto found = std::find(m_previewMeshes.begin(), m_previewMeshes.end(), mesh);
    if (found == m_previewMeshes.end()) {
        return false;
    }

    m_previewMeshes.erase(found);
    return true;
}

std::vector<uint8_t> ASkeleton::serialize() const
{
    std::vector<uint8_t> skeletonBlob = m_skeleton->serialize();

    ASkeletonBlobHeader header;
    header.previewMeshCount = static_cast<uint32_t>(m_previewMeshes.size());
    header.previewMeshesOffset = sizeof(ASkeletonBlobHeader);
    header.skeletonOffset = header.previewMeshesOffset + header.previewMeshCount * static_cast<uint32_t>(sizeof(AssetHandle));

    std::vector<uint8_t> blob(header.skeletonOffset + skeletonBlob.size());
    std::memcpy(blob.data(), &header, sizeof(ASkeletonBlobHeader));

    if (header.previewMeshCount > 0) {
        std::memcpy(blob.data() + header.previewMeshesOffset, m_previewMeshes.data(),
                    header.previewMeshCount * sizeof(AssetHandle));
    }
    std::memcpy(blob.data() + header.skeletonOffset, skeletonBlob.data(), skeletonBlob.size());

    return blob;
}

std::unique_ptr<ASkeleton> ASkeleton::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(ASkeletonBlobHeader)) {
        RP_CORE_ERROR("skeleton blob is smaller than its header");
        return nullptr;
    }

    ASkeletonBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(ASkeletonBlobHeader));

    if (header.magic != SKELETON_ASSET_MAGIC) {
        RP_CORE_ERROR("blob is not a skeleton");
        return nullptr;
    }

    uint16_t major = static_cast<uint16_t>(header.version >> 16);
    if (major != SKELETON_ASSET_VERSION_MAJOR) {
        RP_CORE_ERROR("skeleton blob major version {} is incompatible with {}", major, SKELETON_ASSET_VERSION_MAJOR);
        return nullptr;
    }
    if (header.version != SKELETON_ASSET_VERSION) {
        RP_CORE_WARN("skeleton blob minor version differs from {}, reading the known header fields", SKELETON_ASSET_VERSION_MINOR);
    }

    if (header.skeletonOffset > blob.size() ||
        header.previewMeshesOffset + header.previewMeshCount * sizeof(AssetHandle) > blob.size()) {
        RP_CORE_ERROR("skeleton blob sections are out of range");
        return nullptr;
    }

    std::vector<AssetHandle> previewMeshes(header.previewMeshCount);
    if (header.previewMeshCount > 0) {
        std::memcpy(previewMeshes.data(), blob.data() + header.previewMeshesOffset, header.previewMeshCount * sizeof(AssetHandle));
    }

    std::unique_ptr<Skeleton> skeleton = Skeleton::deserialize(blob.subspan(header.skeletonOffset));
    if (skeleton == nullptr) {
        return nullptr;
    }

    return std::make_unique<ASkeleton>(std::move(skeleton), std::move(previewMeshes));
}

} // namespace Rapture
