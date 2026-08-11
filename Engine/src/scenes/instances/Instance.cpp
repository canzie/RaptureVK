#include "Instance.h"

#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_CLASS = "class";
static constexpr std::string_view KEY_ID = "id";
static constexpr std::string_view KEY_NAME = "name";

Instance::Instance(Scene &scene, std::string_view name) : m_scene(&scene), m_id(UUIDGenerator::Generate()), m_name(name) {}

Instance::~Instance()
{
    onDestroy.fire(this);
}

void Instance::remintId()
{
    m_id = UUIDGenerator::Generate();
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
}

void Instance::serialize(WriteNode node) const
{
    node.set(KEY_CLASS, type().name);
    node.set(KEY_ID, m_id);
    node.set(KEY_NAME, std::string_view(m_name));
}

void Instance::deserialize(ReadNode node)
{
    // the construction that got us here minted an id, the document's one replaces it
    m_id = node.child(KEY_ID).asU64(m_id);
    setName(node.child(KEY_NAME).asString(m_name));
}

std::string_view Instance::readClassName(ReadNode node)
{
    return node.child(KEY_CLASS).asString();
}

} // namespace Rapture
