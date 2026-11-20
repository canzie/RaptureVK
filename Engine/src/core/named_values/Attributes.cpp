#include <rapture/core/named_values/Attributes.h>

#include <rapture/core/utils/Log.h>

namespace Rapture {

static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_TYPE = "type";
static constexpr std::string_view KEY_VALUE = "value";

static constexpr std::string_view TYPE_FLOAT = "float";
static constexpr std::string_view TYPE_BOOL = "bool";
static constexpr std::string_view TYPE_INT = "int";
static constexpr std::string_view TYPE_VEC3 = "vec3";

static void s_writeVec3(WriteNode node, std::string_view key, const glm::vec3 &value)
{
    WriteNode array = node.addArray(key);
    array.append(value.x);
    array.append(value.y);
    array.append(value.z);
}

static glm::vec3 s_readVec3(ReadNode array)
{
    if (array.size() != 3) {
        return glm::vec3(0.0f);
    }
    return glm::vec3(static_cast<float>(array.at(0).asF64()), static_cast<float>(array.at(1).asF64()),
                     static_cast<float>(array.at(2).asF64()));
}

/**
 * @brief Drops whatever another type holds under a name, so a write of a new type can take it over
 * @param attributes The set written to
 * @param name The name about to be written
 * @param type The type about to be written
 */
static void s_releaseNameFromOtherType(Attributes &attributes, std::string_view name, ValueType type)
{
    const ValueType held = attributes.typeOf(name);
    if (held != ValueType::NONE && held != type) {
        attributes.remove(name);
    }
}

void Attributes::setFloat(std::string_view name, float value)
{
    s_releaseNameFromOtherType(*this, name, ValueType::FLOAT);
    m_values.set(m_values.addFloat(name, value), value);
    onChanged.fire(name);
}

void Attributes::setBool(std::string_view name, bool value)
{
    s_releaseNameFromOtherType(*this, name, ValueType::BOOL);
    m_values.set(m_values.addBool(name, value), value);
    onChanged.fire(name);
}

void Attributes::setInt(std::string_view name, int32_t value)
{
    s_releaseNameFromOtherType(*this, name, ValueType::INT);
    m_values.set(m_values.addInt(name, value), value);
    onChanged.fire(name);
}

void Attributes::setVec3(std::string_view name, const glm::vec3 &value)
{
    s_releaseNameFromOtherType(*this, name, ValueType::VEC3);
    m_values.set(m_values.addVec3(name, value), value);
    onChanged.fire(name);
}

float Attributes::getFloat(std::string_view name, float fallback) const
{
    NamedValues::FloatKey key = m_values.findFloat(name);
    return key.isValid() ? m_values.get(key) : fallback;
}

bool Attributes::getBool(std::string_view name, bool fallback) const
{
    NamedValues::BoolKey key = m_values.findBool(name);
    return key.isValid() ? m_values.get(key) : fallback;
}

int32_t Attributes::getInt(std::string_view name, int32_t fallback) const
{
    NamedValues::IntKey key = m_values.findInt(name);
    return key.isValid() ? m_values.get(key) : fallback;
}

glm::vec3 Attributes::getVec3(std::string_view name, const glm::vec3 &fallback) const
{
    NamedValues::Vec3Key key = m_values.findVec3(name);
    return key.isValid() ? m_values.get(key) : fallback;
}

ValueType Attributes::typeOf(std::string_view name) const
{
    for (const NamedValues::Entry &entry : m_values.entries()) {
        if (entry.name == name) {
            return entry.type;
        }
    }
    return ValueType::NONE;
}

bool Attributes::remove(std::string_view name)
{
    if (!has(name)) {
        return false;
    }

    // storage is indexed by add order, so the rest are added again into a set without the dropped one
    NamedValues kept;
    for (const NamedValues::Entry &entry : m_values.entries()) {
        if (entry.name == name) {
            continue;
        }

        switch (entry.type) {
        case ValueType::FLOAT:
            kept.addFloat(entry.name, m_values.get(NamedValues::FloatKey{{entry.index}}));
            break;
        case ValueType::BOOL:
            kept.addBool(entry.name, m_values.get(NamedValues::BoolKey{{entry.index}}));
            break;
        case ValueType::INT:
            kept.addInt(entry.name, m_values.get(NamedValues::IntKey{{entry.index}}));
            break;
        case ValueType::VEC3:
            kept.addVec3(entry.name, m_values.get(NamedValues::Vec3Key{{entry.index}}));
            break;
        case ValueType::NONE:
            break;
        }
    }
    m_values = std::move(kept);

    onChanged.fire(name);
    return true;
}

void Attributes::serialize(WriteNode node) const
{
    for (const NamedValues::Entry &entry : m_values.entries()) {
        WriteNode value = node.appendObject();
        value.set(KEY_NAME, std::string_view(entry.name));

        switch (entry.type) {
        case ValueType::FLOAT:
            value.set(KEY_TYPE, TYPE_FLOAT);
            value.set(KEY_VALUE, m_values.get(NamedValues::FloatKey{{entry.index}}));
            break;
        case ValueType::BOOL:
            value.set(KEY_TYPE, TYPE_BOOL);
            value.set(KEY_VALUE, m_values.get(NamedValues::BoolKey{{entry.index}}));
            break;
        case ValueType::INT:
            value.set(KEY_TYPE, TYPE_INT);
            value.set(KEY_VALUE, static_cast<int64_t>(m_values.get(NamedValues::IntKey{{entry.index}})));
            break;
        case ValueType::VEC3:
            value.set(KEY_TYPE, TYPE_VEC3);
            s_writeVec3(value, KEY_VALUE, m_values.get(NamedValues::Vec3Key{{entry.index}}));
            break;
        case ValueType::NONE:
            break;
        }
    }
}

void Attributes::deserialize(ReadNode node)
{
    m_values.clear();

    for (size_t i = 0; i < node.size(); i++) {
        ReadNode value = node.at(i);
        const std::string_view name = value.child(KEY_NAME).asString();
        const std::string_view type = value.child(KEY_TYPE).asString();
        ReadNode stored = value.child(KEY_VALUE);

        if (type == TYPE_FLOAT) {
            m_values.addFloat(name, static_cast<float>(stored.asF64()));
        } else if (type == TYPE_BOOL) {
            m_values.addBool(name, stored.asBool());
        } else if (type == TYPE_INT) {
            m_values.addInt(name, static_cast<int32_t>(stored.asI64()));
        } else if (type == TYPE_VEC3) {
            m_values.addVec3(name, s_readVec3(stored));
        } else {
            RP_CORE_WARN("attribute '{}' has unknown type '{}', dropping it", name, type);
        }
    }
}

} // namespace Rapture
