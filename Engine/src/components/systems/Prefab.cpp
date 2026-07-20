#include "Prefab.h"

#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "components/HierarchyComponent.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include <cstring>
#include <vector>

namespace Rapture {

static constexpr uint32_t PREFAB_BLOB_MAGIC = 0x46505052; // "RPPF", identifies the blob as a prefab

// Version packs major in the high 16 bits and minor in the low 16, matching the mesh and texture blobs.
static constexpr uint16_t PREFAB_BLOB_VERSION_MAJOR = 1;
static constexpr uint16_t PREFAB_BLOB_VERSION_MINOR = 0;
static constexpr uint32_t PREFAB_BLOB_VERSION =
    (static_cast<uint32_t>(PREFAB_BLOB_VERSION_MAJOR) << 16) | PREFAB_BLOB_VERSION_MINOR;

// Fixed 64-byte directory at the start of every prefab blob. The reserved tail absorbs new fields
// without moving the sections; grow the header and bump the major version when it runs out.
struct PrefabBlobHeader {
    uint32_t magic = PREFAB_BLOB_MAGIC;
    uint32_t version = PREFAB_BLOB_VERSION;
    uint32_t nodeCount = 0;
    uint32_t nameOffset = 0;  // byte offset of the name section
    uint32_t nodesOffset = 0; // byte offset of the node section
    uint32_t reserved[11] = {};
};

static_assert(sizeof(PrefabBlobHeader) == 64,
              "prefab blob header is a fixed 64-byte directory, bump to 128 and the major version if it must grow");

std::vector<uint8_t> Prefab::serialize() const
{
    auto appendBytes = [](std::vector<uint8_t> &out, const void *data, size_t size) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    auto appendString = [&](std::vector<uint8_t> &out, const std::string &s) {
        uint32_t length = static_cast<uint32_t>(s.size());
        appendBytes(out, &length, sizeof(length));
        appendBytes(out, s.data(), s.size());
    };

    std::vector<uint8_t> nameSection;
    appendString(nameSection, m_name);

    std::vector<uint8_t> nodeSection;
    for (const Node &node : m_nodes) {
        appendString(nodeSection, node.name);
        appendBytes(nodeSection, &node.parent, sizeof(node.parent));
        appendBytes(nodeSection, &node.localTransform, sizeof(node.localTransform));
        appendBytes(nodeSection, &node.mesh, sizeof(node.mesh));
        appendBytes(nodeSection, &node.material, sizeof(node.material));
        appendBytes(nodeSection, &node.boundingBoxMin, sizeof(node.boundingBoxMin));
        appendBytes(nodeSection, &node.boundingBoxMax, sizeof(node.boundingBoxMax));
    }

    PrefabBlobHeader header;
    header.nodeCount = static_cast<uint32_t>(m_nodes.size());
    header.nameOffset = sizeof(PrefabBlobHeader);
    header.nodesOffset = static_cast<uint32_t>(sizeof(PrefabBlobHeader) + nameSection.size());

    std::vector<uint8_t> blob(sizeof(PrefabBlobHeader));
    std::memcpy(blob.data(), &header, sizeof(PrefabBlobHeader));
    blob.insert(blob.end(), nameSection.begin(), nameSection.end());
    blob.insert(blob.end(), nodeSection.begin(), nodeSection.end());
    return blob;
}

std::unique_ptr<Prefab> Prefab::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(PrefabBlobHeader)) {
        RP_CORE_ERROR("Prefab blob is smaller than its header");
        return nullptr;
    }

    PrefabBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(PrefabBlobHeader));
    if (header.magic != PREFAB_BLOB_MAGIC) {
        RP_CORE_ERROR("Prefab blob has an invalid magic");
        return nullptr;
    }
    if ((header.version >> 16) != PREFAB_BLOB_VERSION_MAJOR) {
        RP_CORE_ERROR("Prefab blob major version {} is unsupported", header.version >> 16);
        return nullptr;
    }

    size_t offset = header.nameOffset;
    auto read = [&](void *dst, size_t size) -> bool {
        if (offset + size > blob.size()) {
            return false;
        }
        std::memcpy(dst, blob.data() + offset, size);
        offset += size;
        return true;
    };
    auto readString = [&](std::string &s) -> bool {
        uint32_t length = 0;
        if (!read(&length, sizeof(length)) || offset + length > blob.size()) {
            return false;
        }
        s.assign(reinterpret_cast<const char *>(blob.data() + offset), length);
        offset += length;
        return true;
    };

    auto prefab = std::make_unique<Prefab>();
    std::string name;
    if (!readString(name)) {
        RP_CORE_ERROR("Prefab blob name is truncated");
        return nullptr;
    }
    prefab->setName(std::move(name));

    offset = header.nodesOffset;
    std::vector<Node> nodes;
    nodes.reserve(header.nodeCount);
    for (uint32_t i = 0; i < header.nodeCount; ++i) {
        Node node;
        if (!readString(node.name) || !read(&node.parent, sizeof(node.parent)) ||
            !read(&node.localTransform, sizeof(node.localTransform)) || !read(&node.mesh, sizeof(node.mesh)) ||
            !read(&node.material, sizeof(node.material)) || !read(&node.boundingBoxMin, sizeof(node.boundingBoxMin)) ||
            !read(&node.boundingBoxMax, sizeof(node.boundingBoxMax))) {
            RP_CORE_ERROR("Prefab blob node {} is truncated", i);
            return nullptr;
        }
        nodes.push_back(std::move(node));
    }

    prefab->setNodes(std::move(nodes));
    return prefab;
}

Entity Prefab::instantiate(AssetRef prefabRef, Scene *scene, const glm::mat4 &rootTransform)
{
    if (scene == nullptr) {
        RP_CORE_ERROR("Cannot instantiate prefab into a null scene");
        return Entity::null();
    }

    if (!prefabRef) {
        RP_CORE_ERROR("Cannot instantiate an invalid prefab ref");
        return Entity::null();
    }

    Prefab *prefab = prefabRef.get()->getUnderlyingAsset<Prefab>();
    if (prefab == nullptr) {
        RP_CORE_ERROR("Prefab ref does not hold a prefab asset");
        return Entity::null();
    }

    Entity root = scene->createEntity(prefab->m_name);
    root.addComponent<TransformComponent>(rootTransform);
    root.addComponent<PrefabComponent>(prefabRef);

    const std::vector<Node> &nodes = prefab->m_nodes;
    if (nodes.empty()) {
        RP_CORE_WARN("Instantiating prefab '{}' with no nodes", prefab->m_name);
    }

    std::vector<Entity> entities(nodes.size());
    std::vector<glm::mat4> worldTransforms(nodes.size());

    // Nodes are pre-order, so a parent is processed before its children. Transforms are flattened
    // to world here because transform propagation does not exist yet.
    for (size_t i = 0; i < nodes.size(); i++) {
        const Node &node = nodes[i];

        glm::mat4 parentWorld = (node.parent >= 0) ? worldTransforms[node.parent] : rootTransform;
        worldTransforms[i] = parentWorld * node.localTransform;

        Entity entity = scene->createEntity(node.name);
        entity.addComponent<TransformComponent>(worldTransforms[i]);
        entities[i] = entity;

        Entity parentEntity = (node.parent >= 0) ? entities[node.parent] : root;
        HierarchyComponent::setParent(entity, parentEntity);

        if (!node.hasMesh()) {
            continue;
        }

        AssetRef meshRef = AssetManager::getAsset(node.mesh);
        if (!meshRef) {
            RP_CORE_ERROR("Prefab '{}' node '{}' references missing mesh {}", prefab->m_name, node.name, node.mesh);
            continue;
        }

        auto &meshComp = entity.addComponent<MeshComponent>(meshRef);
        if (meshComp.mesh) {
            if (node.hasBoundingBox()) {
                entity.addComponent<BoundingBoxComponent>(node.boundingBoxMin, node.boundingBoxMax);
            }
            entity.addComponent<BLASComponent>(meshComp.mesh);
            scene->registerBLAS(entity);
        }

        if (node.material != INVALID_ASSET_HANDLE) {
            AssetRef matRef = AssetManager::getAsset(node.material);
            if (matRef) {
                entity.addComponent<MaterialComponent>(matRef);
            } else {
                RP_CORE_WARN("Prefab '{}' node '{}' references missing material {}", prefab->m_name, node.name, node.material);
            }
        }
    }

    return root;
}

} // namespace Rapture
