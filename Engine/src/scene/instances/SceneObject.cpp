#include <rapture/scene/instances/SceneObject.h>

#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/SceneLoadContext.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/InstanceRegistry.h>

#include <algorithm>

namespace Rapture {

static constexpr std::string_view KEY_ID = "id";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_COMPONENTS = "components";
static constexpr std::string_view KEY_CHILDREN = "children";
static constexpr std::string_view KEY_IN_OUTLINER = "inOutliner";
static constexpr std::string_view KEY_ATTRIBUTES = "attributes";

SceneObject::SceneObject(Scene &scene, std::string_view name) : Instance(scene, name)
{
    m_entity = scene.createEntity(std::string(name));
    m_entity.set<InstanceComponent>(this);
}

static void s_collectSubtree(SceneObject &object, std::vector<Instance *> &out)
{
    out.push_back(&object);

    for (const auto &component : object.components()) {
        out.push_back(component.get());
    }

    const bool includeInternal = true;
    for (const auto &child : object.children(includeInternal)) {
        s_collectSubtree(*child, out);
    }
}

SceneObject::~SceneObject()
{
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

void SceneObject::beginSubtreeDestruction(SceneObject &object)
{
    std::vector<Instance *> dying;
    s_collectSubtree(object, dying);

    for (Instance *instance : dying) {
        instance->m_isBeingDestroyed = true;
    }

    // all of the subtree is marked before any of it is told, so no listener can pull a part of it out
    for (Instance *instance : dying) {
        instance->onDestroy.fire(instance);
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

SceneObject *SceneObject::addChild(std::unique_ptr<SceneObject> child, InternalMode internalMode)
{
    RP_ASSERT(child != nullptr, "a scene object cannot adopt nothing");

    if (isBeingDestroyed()) {
        RP_CORE_ERROR("'{}' is being destroyed and cannot adopt '{}'", name(), child->name());
        beginSubtreeDestruction(*child);
        return nullptr;
    }

    if (internalMode == InternalMode::ENABLED) {
        child->m_flags |= SCENE_OBJECT_FLAG_INTERNAL;
    } else {
        child->m_flags &= ~SCENE_OBJECT_FLAG_INTERNAL;
    }

    SceneObject *adopted = child.get();
    linkChild(std::move(child));

    adopted->onParentChanged();
    return adopted;
}

void SceneObject::removeChild(SceneObject *child)
{
    RP_ASSERT(child != nullptr, "a scene object cannot remove nothing");

    if (child->m_parent != this || child->isBeingDestroyed()) {
        return;
    }

    beginSubtreeDestruction(*child);

    // unlinked after the listeners ran, since they may have changed the other children
    unlinkChild(child).reset();
}

bool SceneObject::setParent(SceneObject *parent)
{
    RP_ASSERT(parent != nullptr, "a scene object cannot be moved under nothing");

    if (m_parent == parent) {
        return true;
    }

    if (m_parent == nullptr) {
        RP_CORE_ERROR("'{}' has no parent to be moved away from", name());
        return false;
    }

    if (isInternal()) {
        RP_CORE_ERROR("'{}' is internal to '{}' and cannot be moved out of it", name(), m_parent->name());
        return false;
    }

    if (isBeingDestroyed() || parent->isBeingDestroyed()) {
        RP_CORE_ERROR("'{}' cannot be moved under '{}' while either is being destroyed", name(), parent->name());
        return false;
    }

    if (parent->isInSubtreeOf(*this)) {
        RP_CORE_ERROR("'{}' cannot be moved under itself", name());
        return false;
    }

    parent->linkChild(m_parent->unlinkChild(this));

    onParentChanged();
    return true;
}

bool SceneObject::isInSubtreeOf(const SceneObject &root) const
{
    for (const SceneObject *walk = this; walk != nullptr; walk = walk->m_parent) {
        if (walk == &root) {
            return true;
        }
    }

    return false;
}

void SceneObject::linkChild(std::unique_ptr<SceneObject> child)
{
    child->m_parent = this;

    if (child->isInternal()) {
        m_children.push_back(std::move(child));
        m_internalCount++;
        return;
    }

    m_children.insert(m_children.end() - static_cast<ptrdiff_t>(m_internalCount), std::move(child));
}

std::unique_ptr<SceneObject> SceneObject::unlinkChild(SceneObject *child)
{
    auto found = std::find_if(m_children.begin(), m_children.end(),
                              [child](const std::unique_ptr<SceneObject> &owned) { return owned.get() == child; });
    RP_ASSERT(found != m_children.end(), "'{}' is not a child of '{}'", child->name(), name());

    if (child->isInternal()) {
        m_internalCount--;
    }

    std::unique_ptr<SceneObject> owned = std::move(*found);
    m_children.erase(found);
    owned->m_parent = nullptr;
    return owned;
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

SceneComponent *SceneObject::addComponent(std::unique_ptr<SceneComponent> component)
{
    if (component == nullptr) {
        return nullptr;
    }

    if (isBeingDestroyed()) {
        RP_CORE_ERROR("'{}' is being destroyed and cannot take on '{}'", name(), component->name());
        return nullptr;
    }

    SceneComponent *added = component.get();
    m_components.push_back(std::move(component));

    added->attachTo(this);
    return added;
}

void SceneObject::removeComponent(SceneComponent *component)
{
    if (component == nullptr || component->owner() != this || component->isBeingDestroyed()) {
        return;
    }

    component->m_isBeingDestroyed = true;
    component->onDestroy.fire(component);

    // found after the listeners ran, since they may have changed the other components
    auto found = std::find_if(m_components.begin(), m_components.end(),
                              [component](const std::unique_ptr<SceneComponent> &owned) { return owned.get() == component; });
    std::unique_ptr<SceneComponent> owned = std::move(*found);
    m_components.erase(found);

    owned->detach();
}

void SceneObject::serialize(WriteNode node) const
{
    Instance::serialize(node);

    if (!m_visibility.inOutliner) {
        node.set(KEY_IN_OUTLINER, false);
    }

    if (!m_attributes.values().entries().empty()) {
        m_attributes.serialize(node.addArray(KEY_ATTRIBUTES));
    }

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

void SceneObject::deserialize(ReadNode node)
{
    Instance::deserialize(node);

    m_visibility.inOutliner = node.child(KEY_IN_OUTLINER).asBool(true);
    m_attributes.deserialize(node.child(KEY_ATTRIBUTES));
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
    if (!loadComponents(header, context)) {
        return false;
    }

    for (size_t i = 0; i < header.children.size(); i++) {
        if (loadSubtree(*this, header.children.at(i), context) == nullptr) {
            return false;
        }
    }

    return true;
}

bool SceneObject::replaceComponents(const DocumentHeader &header, SceneLoadContext &context)
{
    while (!m_components.empty()) {
        removeComponent(m_components.back().get());
    }

    return loadComponents(header, context);
}

bool SceneObject::loadComponents(const DocumentHeader &header, SceneLoadContext &context)
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

        if (addComponent(std::move(component)) == nullptr) {
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
    SceneLoadContext context(*parent.scene(), true);

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

    SceneObject *self = parent.addChild(std::move(created), internalMode);
    if (self == nullptr) {
        return nullptr;
    }

    context.addInstance(header.id, self);
    self->deserialize(node);

    if (context.remintsIds()) {
        self->remintId();
    }

    if (!self->loadContents(header, context)) {
        // what was read before the failure is not left behind, and the parent takes itself out in turn
        parent.removeChild(self);
        return nullptr;
    }

    return self;
}

} // namespace Rapture
