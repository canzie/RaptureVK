#include "SceneObject.h"

#include "scene/components/Components.h"
#include "core/utils/Log.h"
#include "core/utils/rp_assert.h"
#include "scene/Scene.h"
#include "scene/SceneLoadContext.h"
#include "scene/instances/InstanceRegistry.h"

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

void SceneObject::setSerialized(bool serialized)
{
    if (serialized) {
        m_flags |= SCENE_OBJECT_FLAG_SERIALIZED;
    } else {
        m_flags &= ~SCENE_OBJECT_FLAG_SERIALIZED;
    }
}

std::span<const std::unique_ptr<SceneObject>> SceneObject::children(bool includeInternal) const
{
    const size_t count = includeInternal ? m_children.size() : m_children.size() - m_internalCount;
    return std::span<const std::unique_ptr<SceneObject>>(m_children.data(), count);
}

void SceneObject::addChild(std::unique_ptr<SceneObject> child, InternalMode internalMode)
{
    RP_ASSERT(child != nullptr, "a scene object cannot adopt nothing");

    child->m_parent = this;
    if (internalMode == InternalMode::ENABLED) {
        child->m_flags |= SCENE_OBJECT_FLAG_INTERNAL;
    } else {
        child->m_flags &= ~SCENE_OBJECT_FLAG_INTERNAL;
    }

    SceneObject *adopted = child.get();

    if (adopted->isInternal()) {
        m_children.push_back(std::move(child));
        m_internalCount++;
    } else {
        m_children.insert(m_children.end() - static_cast<ptrdiff_t>(m_internalCount), std::move(child));
    }

    adopted->onParentChanged();
}

std::unique_ptr<SceneObject> SceneObject::removeChild(SceneObject *child)
{
    RP_ASSERT(child != nullptr, "a scene object cannot release nothing");

    if (child->isInternal()) {
        RP_CORE_ERROR("'{}' is internal to '{}' and cannot be taken from it", child->name(), name());
        return nullptr;
    }

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

void SceneObject::destroyChild(SceneObject *child)
{
    RP_ASSERT(child != nullptr, "a scene object cannot destroy nothing");

    for (size_t i = 0; i < m_children.size(); i++) {
        if (m_children[i].get() != child) {
            continue;
        }

        if (child->isInternal()) {
            m_internalCount--;
        }

        m_children.erase(m_children.begin() + static_cast<ptrdiff_t>(i));
        return;
    }
}

void SceneObject::onParentChanged()
{
    const bool includeInternal = true;

    for (const auto &child : children(includeInternal)) {
        child->onParentChanged();
    }
}

SceneObject *SceneObject::findChild(std::string_view name, bool includeInternal) const
{
    for (const auto &child : children(includeInternal)) {
        if (child->name() == name) {
            return child.get();
        }
    }

    return nullptr;
}

SceneObject *SceneObject::findDescendant(std::string_view path, bool includeInternal) const
{
    const SceneObject *current = this;
    SceneObject *found = nullptr;
    size_t start = 0;

    while (start < path.size()) {
        size_t slash = path.find('/', start);
        size_t length = (slash == std::string_view::npos) ? path.size() - start : slash - start;
        std::string_view step = path.substr(start, length);

        if (!step.empty()) {
            found = current->findChild(step, includeInternal);
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
        if (!child->isSerialized()) {
            continue;
        }
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

bool SceneObject::loadContents(const DocumentHeader &header, SceneLoadContext &context)
{
    for (size_t i = 0; i < header.components.size(); i++) {
        ReadNode entry = header.components.at(i);
        std::string_view className = readClassName(entry);

        std::unique_ptr<SceneComponent> component = InstanceRegistry::createComponent(className, *scene(), className);
        if (component == nullptr) {
            RP_CORE_ERROR("no scene component class named '{}', needed by '{}'", className, name());
            return false;
        }

        SceneComponent *raw = component.get();

        // read before attaching so the component claims what it needs from its authored fields
        component->deserialize(entry);
        context.addInstance(entry.child(KEY_ID).asU64(INVALID_INSTANCE_ID), raw);

        if (context.remintsIds()) {
            raw->remintId();
        }

        attachComponent(std::move(component));
    }

    for (size_t i = 0; i < header.children.size(); i++) {
        if (loadSubtree(*this, header.children.at(i), context) == nullptr) {
            return false;
        }
    }

    return true;
}

SceneObject *SceneObject::spawnSubtree(SceneObject &parent, ReadNode node, InternalMode internalMode)
{
    if (parent.scene() == nullptr) {
        RP_CORE_ERROR("a subtree spawns into a scene, and '{}' is in none", parent.name());
        return nullptr;
    }

    // what spawns is its own object, not the one it was read from
    SceneLoadContext context(true);

    SceneObject *root = loadSubtree(parent, node, context, internalMode);
    if (root == nullptr) {
        RP_CORE_ERROR("subtree could not be read into the scene");
        return nullptr;
    }

    context.finish();

    return root;
}

SceneObject *SceneObject::loadSubtree(SceneObject &parent, ReadNode node, SceneLoadContext &context, InternalMode internalMode)
{
    DocumentHeader header = readHeader(node);

    std::unique_ptr<SceneObject> created = InstanceRegistry::createObject(header.className, *parent.scene(), header.name);
    if (created == nullptr) {
        RP_CORE_ERROR("no scene object class named '{}', needed by '{}'", header.className, header.name);
        return nullptr;
    }

    SceneObject *self = created.get();
    parent.addChild(std::move(created), internalMode);
    context.addInstance(header.id, self);
    self->deserialize(node);

    if (context.remintsIds()) {
        self->remintId();
    }

    if (!self->loadContents(header, context)) {
        // what was read before the failure is not left behind, and the parent takes itself out in turn
        parent.destroyChild(self);
        return nullptr;
    }

    return self;
}

} // namespace Rapture
