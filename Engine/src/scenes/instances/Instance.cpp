#include "Instance.h"

#include "components/Components.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "scenes/instances/InstanceRegistry.h"

namespace Rapture {

static constexpr std::string_view KEY_CLASS = "class";
static constexpr std::string_view KEY_ID = "id";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_CHILDREN = "children";

Instance::Instance(Scene &scene, std::string_view name) : m_scene(&scene), m_id(UUIDGenerator::Generate()), m_name(name)
{
    m_entity = scene.createEntity(m_name);
    m_entity.setComponent<InstanceComponent>(this);
}

void Instance::remintId()
{
    m_id = UUIDGenerator::Generate();
}

Instance::~Instance()
{
    onDestroy.fire(this);

    m_children.clear();

    if (m_entity.isValid()) {
        m_entity.destroy();
    }
}

const TypeInfo &Instance::staticType()
{
    static const TypeInfo type("Instance", nullptr);
    return type;
}

const TypeInfo &Instance::type() const
{
    return staticType();
}

void Instance::setName(std::string_view name)
{
    m_name = name;

    if (auto *tag = m_entity.tryGetComponent<TagComponent>()) {
        tag->tag = m_name;
    }
}

void Instance::addChild(std::unique_ptr<Instance> child)
{
    if (child == nullptr) {
        return;
    }

    child->m_parent = this;
    Instance *adopted = child.get();
    m_children.push_back(std::move(child));

    adopted->onParentChanged();
}

std::unique_ptr<Instance> Instance::removeChild(Instance *child)
{
    for (size_t i = 0; i < m_children.size(); i++) {
        if (m_children[i].get() != child) {
            continue;
        }

        std::unique_ptr<Instance> owned = std::move(m_children[i]);
        m_children.erase(m_children.begin() + static_cast<ptrdiff_t>(i));
        owned->m_parent = nullptr;

        owned->onParentChanged();
        return owned;
    }

    return nullptr;
}

void Instance::onParentChanged()
{
    for (const auto &child : m_children) {
        child->onParentChanged();
    }
}

Instance *Instance::findChild(std::string_view name) const
{
    for (const auto &child : m_children) {
        if (child->m_name == name) {
            return child.get();
        }
    }

    return nullptr;
}

Instance *Instance::findDescendant(std::string_view path) const
{
    const Instance *current = this;
    Instance *found = nullptr;
    size_t start = 0;

    while (start < path.size()) {
        size_t slash = path.find('/', start);
        size_t length = (slash == std::string_view::npos) ? path.size() - start : slash - start;
        std::string_view step = path.substr(start, length);

        if (!step.empty()) {
            found = current->findChild(step);
            if (found == nullptr) {
                return nullptr;
            }
            current = found;
        }

        start = (slash == std::string_view::npos) ? path.size() : slash + 1;
    }

    return found;
}

void Instance::serialize(WriteNode node) const
{
    node.set(KEY_CLASS, type().name);
    node.set(KEY_ID, m_id);
    node.set(KEY_NAME, std::string_view(m_name));

    if (m_children.empty()) {
        return;
    }

    WriteNode children = node.addArray(KEY_CHILDREN);
    for (const auto &child : m_children) {
        child->serialize(children.appendObject());
    }
}

void Instance::deserialize(ReadNode node)
{
    // the construction that got us here minted an id, the document's one replaces it
    m_id = node.child(KEY_ID).asU64(m_id);
    setName(node.child(KEY_NAME).asString(m_name));
}

Instance::DocumentHeader Instance::readHeader(ReadNode node)
{
    DocumentHeader header;
    header.className = node.child(KEY_CLASS).asString();
    header.name = node.child(KEY_NAME).asString();
    header.id = node.child(KEY_ID).asU64(INVALID_INSTANCE_ID);
    header.children = node.child(KEY_CHILDREN);
    return header;
}

bool Instance::loadSubtree(Instance &parent, ReadNode node, std::vector<Instance *> &order)
{
    DocumentHeader header = readHeader(node);

    std::unique_ptr<Instance> created = InstanceRegistry::create(header.className, *parent.scene(), header.name);
    if (created == nullptr) {
        RP_CORE_ERROR("no instance class named '{}', needed by '{}'", header.className, header.name);
        return false;
    }

    Instance *self = created.get();
    parent.addChild(std::move(created));
    order.push_back(self);
    self->deserialize(node);

    for (size_t i = 0; i < header.children.size(); i++) {
        if (!loadSubtree(*self, header.children.at(i), order)) {
            return false;
        }
    }

    return true;
}

} // namespace Rapture
