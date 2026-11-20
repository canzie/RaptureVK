#include <rapture/core/named_values/NamedValues.h>

namespace Rapture {

uint16_t NamedValues::findIndex(std::string_view name, ValueType type) const
{
    const uint64_t hash = hashName(name);
    for (const Entry &entry : m_entries) {
        if (entry.hash == hash && entry.type == type && entry.name == name) {
            return entry.index;
        }
    }
    return INVALID_INDEX;
}

bool NamedValues::addEntry(std::string_view name, ValueType type, uint16_t index)
{
    const uint64_t hash = hashName(name);
    for (const Entry &entry : m_entries) {
        if (entry.hash == hash && entry.name == name) {
            return false;
        }
    }

    m_entries.push_back({.name = std::string(name), .hash = hash, .type = type, .index = index});
    return true;
}

NamedValues::FloatKey NamedValues::addFloat(std::string_view name, float initial)
{
    if (FloatKey existing = findFloat(name); existing.isValid()) {
        return existing;
    }

    const uint16_t index = static_cast<uint16_t>(m_floats.size());
    if (!addEntry(name, ValueType::FLOAT, index)) {
        return {};
    }
    m_floats.push_back(initial);
    return {{index}};
}

NamedValues::BoolKey NamedValues::addBool(std::string_view name, bool initial)
{
    if (BoolKey existing = findBool(name); existing.isValid()) {
        return existing;
    }

    const uint16_t index = static_cast<uint16_t>(m_bools.size());
    if (!addEntry(name, ValueType::BOOL, index)) {
        return {};
    }
    m_bools.push_back(initial ? 1 : 0);
    return {{index}};
}

NamedValues::IntKey NamedValues::addInt(std::string_view name, int32_t initial)
{
    if (IntKey existing = findInt(name); existing.isValid()) {
        return existing;
    }

    const uint16_t index = static_cast<uint16_t>(m_ints.size());
    if (!addEntry(name, ValueType::INT, index)) {
        return {};
    }
    m_ints.push_back(initial);
    return {{index}};
}

NamedValues::Vec3Key NamedValues::addVec3(std::string_view name, const glm::vec3 &initial)
{
    if (Vec3Key existing = findVec3(name); existing.isValid()) {
        return existing;
    }

    const uint16_t index = static_cast<uint16_t>(m_vec3s.size());
    if (!addEntry(name, ValueType::VEC3, index)) {
        return {};
    }
    m_vec3s.push_back(initial);
    return {{index}};
}

NamedValues::FloatKey NamedValues::findFloat(std::string_view name) const
{
    return {{findIndex(name, ValueType::FLOAT)}};
}

NamedValues::BoolKey NamedValues::findBool(std::string_view name) const
{
    return {{findIndex(name, ValueType::BOOL)}};
}

NamedValues::IntKey NamedValues::findInt(std::string_view name) const
{
    return {{findIndex(name, ValueType::INT)}};
}

NamedValues::Vec3Key NamedValues::findVec3(std::string_view name) const
{
    return {{findIndex(name, ValueType::VEC3)}};
}

void NamedValues::clear()
{
    m_entries.clear();
    m_floats.clear();
    m_bools.clear();
    m_ints.clear();
    m_vec3s.clear();
}

} // namespace Rapture
