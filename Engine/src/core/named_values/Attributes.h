#ifndef RAPTURE__ATTRIBUTES_H
#define RAPTURE__ATTRIBUTES_H

#include "core/events/EventSignal.h"
#include "core/named_values/NamedValues.h"
#include "core/serialization/SerialDocument.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace Rapture {

/**
 * @brief Values put on something by whoever is using it, under whatever names they pick.
 *
 * Nothing declares what may be here, so writing a name nothing holds is how one comes to be held.
 */
class Attributes {
  public:
    void setFloat(std::string_view name, float value);
    void setBool(std::string_view name, bool value);
    void setInt(std::string_view name, int32_t value);
    void setVec3(std::string_view name, const glm::vec3 &value);

    /**
     * @brief Reads the float a name goes with
     * @param name The name to look for
     * @param fallback What to answer with if no float goes by that name
     * @return The value, or the fallback
     */
    float getFloat(std::string_view name, float fallback = 0.0f) const;

    /**
     * @brief Reads the bool a name goes with
     * @param name The name to look for
     * @param fallback What to answer with if no bool goes by that name
     * @return The value, or the fallback
     */
    bool getBool(std::string_view name, bool fallback = false) const;

    /**
     * @brief Reads the int a name goes with
     * @param name The name to look for
     * @param fallback What to answer with if no int goes by that name
     * @return The value, or the fallback
     */
    int32_t getInt(std::string_view name, int32_t fallback = 0) const;

    /**
     * @brief Reads the vector a name goes with
     * @param name The name to look for
     * @param fallback What to answer with if no vector goes by that name
     * @return The value, or the fallback
     */
    glm::vec3 getVec3(std::string_view name, const glm::vec3 &fallback = glm::vec3(0.0f)) const;

    /**
     * @brief What type is held under a name
     * @param name The name to look for
     * @return Its type, NONE if nothing goes by that name
     */
    ValueType typeOf(std::string_view name) const;

    bool has(std::string_view name) const { return typeOf(name) != ValueType::NONE; }

    /**
     * @brief Stops holding a name, after which no key taken from this set can be used again
     * @param name The name to drop
     * @return True if something went by that name
     */
    bool remove(std::string_view name);

    const NamedValues &values() const { return m_values; }

    /**
     * @brief Writes what is held here, and what each of them reads
     * @param node Cursor to write into
     */
    void serialize(WriteNode node) const;

    /**
     * @brief Reads back what is held here
     * @param node Cursor to read from
     */
    void deserialize(ReadNode node);

  public:
    /**
     * @brief Fires after a name is written, added or dropped
     */
    EventSignal<void(std::string_view)> onChanged;

  private:
    NamedValues m_values;
};

} // namespace Rapture

#endif // RAPTURE__ATTRIBUTES_H
