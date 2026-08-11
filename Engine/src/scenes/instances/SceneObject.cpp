#include "SceneObject.h"

#include "components/Components.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "scenes/instances/InstanceRegistry.h"

namespace Rapture {

static constexpr std::string_view KEY_ID = "id";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_COMPONENTS = "components";
static constexpr std::string_view KEY_CHILDREN = "children";

SceneObject::SceneObject(Scene &scene, std::string_view name) : Instance(scene, name)
{
    m_entity = scene.createEntity(std::string(name));
    m_entity.set<InstanceComponent>(this);
}

SceneObject::~SceneObject()
{
    onDestroy.fire(this);

    for (const auto &component : m_components) {
        component->detach();
    }
    m_components.clear();

    m_children.clear();

    if (m_entity.isValid() && scene() != nullptr) {
        scene()->destroyEntity(m_entity.getEntity());
    }
}

const TypeInfo &SceneObject::staticType()
{
    static const TypeInfo type("SceneObject", &Instance::staticType());
    return type;
}

const TypeInfo &SceneObject::type() const
{
    return staticType();
}

void SceneObject::setName(std::string_view name)
{
    Instance::setName(name);

    if (m_entity.has<TagComponent>()) {
        m_entity.write<TagComponent>()->tag = std::string(this->name());
    }
}

void SceneObject::addChild(std::unique_ptr<SceneObject> child)
{
    if (child == nullptr) {
        return;
    }

    child->m_parent = this;
    SceneObject *adopted = child.get();
    m_children.push_back(std::move(child));

    adopted->onParentChanged();
}

std::unique_ptr<SceneObject> SceneObject::removeChild(SceneObject *child)
{
    for (size_t i = 0; i < m_children.size(); i++) {
        if (m_children[i].get() != child) {
            continue;
        }

        std::unique_ptr<SceneObject> owned = std::move(m_children[i]);
        m_children.erase(m_children.begin() + static_cast<ptrdiff_t>(i));
        owned->m_parent = nullptr;

        owned->onParentChanged();
        return owned;
    }

    return nullptr;
}

void SceneObject::onParentChanged()
{
    for (const auto &child : m_children) {
        child->onParentChanged();
    }
}

SceneObject *SceneObject::findChild(std::string_view name) const
{
    for (const auto &child : m_children) {
        if (child->name() == name) {
            return child.get();
        }
    }

    return nullptr;
}

SceneObject *SceneObject::findDescendant(std::string_view path) const
{
    const SceneObject *current = this;
    SceneObject *found = nullptr;
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

void SceneObject::attachComponent(std::unique_ptr<SceneComponent> component)
{
    if (component == nullptr) {
        return;
    }

    SceneComponent *attached = component.get();
    m_components.push_back(std::move(component));

    attached->attachTo(this);
}

std::unique_ptr<SceneComponent> SceneObject::detachComponent(SceneComponent *component)
{
    for (size_t i = 0; i < m_components.size(); i++) {
        if (m_components[i].get() != component) {
            continue;
        }

        std::unique_ptr<SceneComponent> owned = std::move(m_components[i]);
        m_components.erase(m_components.begin() + static_cast<ptrdiff_t>(i));
        owned->detach();
        return owned;
    }

    return nullptr;
}

void SceneObject::removeComponent(SceneComponent *component)
{
    detachComponent(component);
}

void SceneObject::serialize(WriteNode node) const
{
    Instance::serialize(node);

    if (!m_components.empty()) {
        WriteNode components = node.addArray(KEY_COMPONENTS);
        for (const auto &component : m_components) {
            component->serialize(components.appendObject());
        }
    }

    if (m_children.empty()) {
        return;
    }

    WriteNode children = node.addArray(KEY_CHILDREN);
    for (const auto &child : m_children) {
        child->serialize(children.appendObject());
    }
}

SceneObject::DocumentHeader SceneObject::readHeader(ReadNode node)
{
    DocumentHeader header;
    header.className = readClassName(node);
    header.name = node.child(KEY_NAME).asString();
    header.id = node.child(KEY_ID).asU64(INVALID_INSTANCE_ID);
    header.components = node.child(KEY_COMPONENTS);
    header.children = node.child(KEY_CHILDREN);
    return header;
}

bool SceneObject::loadContents(const DocumentHeader &header, std::vector<SceneObject *> &order)
{
    for (size_t i = 0; i < header.components.size(); i++) {
        ReadNode entry = header.components.at(i);
        std::string_view className = readClassName(entry);

        std::unique_ptr<SceneComponent> component = InstanceRegistry::createComponent(className, *scene(), className);
        if (component == nullptr) {
            RP_CORE_ERROR("no scene component class named '{}', needed by '{}'", className, name());
            return false;
        }

        // read before attaching so the component claims what it needs from its authored fields
        component->deserialize(entry);
        attachComponent(std::move(component));
    }

    for (size_t i = 0; i < header.children.size(); i++) {
        if (!loadSubtree(*this, header.children.at(i), order)) {
            return false;
        }
    }

    return true;
}

SceneObject *SceneObject::spawnSubtree(SceneObject &parent, ReadNode node)
{
    if (parent.scene() == nullptr) {
        RP_CORE_ERROR("a subtree spawns into a scene, and '{}' is in none", parent.name());
        return nullptr;
    }

    std::vector<SceneObject *> order;
    if (!loadSubtree(parent, node, order) || order.empty()) {
        RP_CORE_ERROR("subtree could not be read into the scene");
        return nullptr;
    }

    // what spawned is its own object, not the one it was read from
    for (SceneObject *object : order) {
        object->remintId();
    }

    return order.front();
}

bool SceneObject::loadSubtree(SceneObject &parent, ReadNode node, std::vector<SceneObject *> &order)
{
    DocumentHeader header = readHeader(node);

    std::unique_ptr<SceneObject> created = InstanceRegistry::createObject(header.className, *parent.scene(), header.name);
    if (created == nullptr) {
        RP_CORE_ERROR("no scene object class named '{}', needed by '{}'", header.className, header.name);
        return false;
    }

    SceneObject *self = created.get();
    parent.addChild(std::move(created));
    order.push_back(self);
    self->deserialize(node);

    return self->loadContents(header, order);
}

} // namespace Rapture
