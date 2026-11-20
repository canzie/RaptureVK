#ifndef RAPTURE__NAMED_VALUES_H
#define RAPTURE__NAMED_VALUES_H

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

enum class ValueType {
    NONE,
    FLOAT,
    BOOL,
    INT,
    VEC3
};

/**
 * @brief A set of typed values that go by name, read and written through a key resolved once.
 *
 * Names belong to whoever adds and looks one up. Nothing a key reaches carries its name.
 */
class NamedValues {
  private:
    static constexpr uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ull;
    static constexpr uint64_t FNV1A_PRIME = 1099511628211ull;

  public:
    static constexpr uint16_t INVALID_INDEX = UINT16_MAX;

    struct Key {
        uint16_t index = INVALID_INDEX;

        bool isValid() const { return index != INVALID_INDEX; }
    };

    struct FloatKey : Key {};
    struct BoolKey : Key {};
    struct IntKey : Key {};
    struct Vec3Key : Key {};

    /**
     * @brief What a value is called and what it holds, for anything offering a choice of them
     */
    struct Entry {
        std::string name;
        uint64_t hash = 0;
        ValueType type = ValueType::NONE;
        uint16_t index = INVALID_INDEX;
    };

  public:
    /**
     * @brief Hashes the name a value goes by
     * @param name The name to hash
     * @return Its hash
     */
    static constexpr uint64_t hashName(std::string_view name)
    {
        uint64_t hash = FNV1A_OFFSET_BASIS;
        for (char c : name) {
            hash ^= static_cast<uint8_t>(c);
            hash *= FNV1A_PRIME;
        }
        return hash;
    }

    /**
     * @brief Adds a float under a name
     * @param name The name it goes by
     * @param initial What it starts at
     * @return Its key, invalid if the name is already taken by another type
     */
    FloatKey addFloat(std::string_view name, float initial);

    /**
     * @brief Adds a bool under a name
     * @param name The name it goes by
     * @param initial What it starts at
     * @return Its key, invalid if the name is already taken by another type
     */
    BoolKey addBool(std::string_view name, bool initial);

    /**
     * @brief Adds an int under a name
     * @param name The name it goes by
     * @param initial What it starts at
     * @return Its key, invalid if the name is already taken by another type
     */
    IntKey addInt(std::string_view name, int32_t initial);

    /**
     * @brief Adds a vector under a name
     * @param name The name it goes by
     * @param initial What it starts at
     * @return Its key, invalid if the name is already taken by another type
     */
    Vec3Key addVec3(std::string_view name, const glm::vec3 &initial);

    /**
     * @brief Finds the float a name goes with
     * @param name The name to look for
     * @return Its key, invalid if no float goes by that name
     */
    FloatKey findFloat(std::string_view name) const;

    /**
     * @brief Finds the bool a name goes with
     * @param name The name to look for
     * @return Its key, invalid if no bool goes by that name
     */
    BoolKey findBool(std::string_view name) const;

    /**
     * @brief Finds the int a name goes with
     * @param name The name to look for
     * @return Its key, invalid if no int goes by that name
     */
    IntKey findInt(std::string_view name) const;

    /**
     * @brief Finds the vector a name goes with
     * @param name The name to look for
     * @return Its key, invalid if no vector goes by that name
     */
    Vec3Key findVec3(std::string_view name) const;

    float get(FloatKey key) const { return m_floats[key.index]; }
    bool get(BoolKey key) const { return m_bools[key.index] != 0; }
    int32_t get(IntKey key) const { return m_ints[key.index]; }
    const glm::vec3 &get(Vec3Key key) const { return m_vec3s[key.index]; }

    void set(FloatKey key, float value) { m_floats[key.index] = value; }
    void set(BoolKey key, bool value) { m_bools[key.index] = value ? 1 : 0; }
    void set(IntKey key, int32_t value) { m_ints[key.index] = value; }
    void set(Vec3Key key, const glm::vec3 &value) { m_vec3s[key.index] = value; }

    /**
     * @brief Drops every value
     */
    void clear();

    const std::vector<Entry> &entries() const { return m_entries; }

  private:
    /**
     * @brief Finds the entry a name and type go with
     * @param name The name to look for
     * @param type The type it has to hold
     * @return Its index into that type's storage, INVALID_INDEX if nothing matches
     */
    uint16_t findIndex(std::string_view name, ValueType type) const;

    /**
     * @brief Records a value under a name, rejecting a name another type already holds
     * @param name The name it goes by
     * @param type The type it holds
     * @param index Its index into that type's storage
     * @return True if the name was free
     */
    bool addEntry(std::string_view name, ValueType type, uint16_t index);

  private:
    std::vector<Entry> m_entries;

    std::vector<float> m_floats;
    std::vector<uint8_t> m_bools;
    std::vector<int32_t> m_ints;
    std::vector<glm::vec3> m_vec3s;
};

} // namespace Rapture

#endif // RAPTURE__NAMED_VALUES_H
