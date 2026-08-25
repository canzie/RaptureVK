#ifndef RAPTURE__BLACKBOARD_H
#define RAPTURE__BLACKBOARD_H

#include "core/named_values/NamedValues.h"
#include "core/serialization/SerialDocument.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Rapture {

/**
 * @brief The values one system reads to decide what to do, written by whatever drives it.
 *
 * What it holds is settled by the reader before anything runs, so a writer naming something it does
 * not hold is answered rather than obeyed.
 */
class Blackboard {
  public:
    struct TriggerKey {
        uint16_t index = NamedValues::INVALID_INDEX;

        bool isValid() const { return index != NamedValues::INVALID_INDEX; }
    };

  public:
    /**
     * @brief Adds a trigger under a name
     * @param name The name it goes by
     * @return Its key, invalid if the name is already taken
     */
    TriggerKey addTrigger(std::string_view name);

    /**
     * @brief Finds the trigger a name goes with
     * @param name The name to look for
     * @return Its key, invalid if no trigger goes by that name
     */
    TriggerKey findTrigger(std::string_view name) const;

    /**
     * @brief Raises a trigger, until something takes it
     * @param key The trigger to raise
     */
    void fire(TriggerKey key);

    /**
     * @brief Takes a raised trigger, lowering it
     * @param key The trigger to take
     * @return True if it was raised
     */
    bool consume(TriggerKey key);

    /**
     * @brief Lowers every trigger nothing took
     */
    void clearTriggers();

    float get(NamedValues::FloatKey key) const { return m_values.get(key); }
    bool get(NamedValues::BoolKey key) const { return m_values.get(key); }
    int32_t get(NamedValues::IntKey key) const { return m_values.get(key); }
    const glm::vec3 &get(NamedValues::Vec3Key key) const { return m_values.get(key); }

    void set(NamedValues::FloatKey key, float value) { m_values.set(key, value); }
    void set(NamedValues::BoolKey key, bool value) { m_values.set(key, value); }
    void set(NamedValues::IntKey key, int32_t value) { m_values.set(key, value); }
    void set(NamedValues::Vec3Key key, const glm::vec3 &value) { m_values.set(key, value); }

    NamedValues &values() { return m_values; }
    const NamedValues &values() const { return m_values; }

    const std::vector<std::string> &triggerNames() const { return m_triggerNames; }

    /**
     * @brief Writes what this blackboard holds, not what it currently reads
     * @param node Cursor to write into
     */
    void serialize(WriteNode node) const;

    /**
     * @brief Reads back what this blackboard holds
     * @param node Cursor to read from
     */
    void deserialize(ReadNode node);

  private:
    NamedValues m_values;
    std::vector<std::string> m_triggerNames;
    std::vector<uint64_t> m_triggerHashes;
    std::vector<uint8_t> m_triggersRaised;
};

} // namespace Rapture

#endif // RAPTURE__BLACKBOARD_H
