#ifndef RAPTURE__UNDO_HISTORY_H
#define RAPTURE__UNDO_HISTORY_H

#include <rapture/core/events/EventSignal.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/**
 * @brief One change to a document that can be taken back and made again
 */
class UndoRecordBase {
  public:
    virtual ~UndoRecordBase() = default;

    /**
     * @brief Puts the document back the way it was before the change
     */
    virtual void undo() = 0;

    /**
     * @brief Makes the change again
     */
    virtual void redo() = 0;

    /**
     * @brief Takes down how the document looks after the change, once; later calls do nothing
     */
    virtual void finish() {}

    /**
     * @brief Whether the change ended where it started, so there is nothing to keep
     * @return True if the document is no different after it
     */
    virtual bool isNoOp() const { return false; }

    /**
     * @brief The memory the record holds on to
     * @return The size in bytes
     */
    virtual size_t byteSize() const = 0;
};

/**
 * @brief The changes one action made, taken back and made again as one step
 */
struct UndoTransaction {
    std::vector<std::unique_ptr<UndoRecordBase>> records;
    size_t byteSize = 0;
};

/**
 * @brief How much of the past a history keeps before it starts forgetting the oldest steps
 */
struct UndoHistoryLimits {
    size_t maxSteps;
    size_t maxBytes;
};

/**
 * @brief The steps one document can be taken back through, and forward again
 */
class UndoHistory {
  public:
    explicit UndoHistory(UndoHistoryLimits limits);

    UndoHistory(const UndoHistory &) = delete;
    UndoHistory &operator=(const UndoHistory &) = delete;

    /**
     * @brief Adds a record to the open transaction, opening one if none is open
     * @param record The change to record
     */
    void record(std::unique_ptr<UndoRecordBase> record);

    /**
     * @brief The records of the transaction changes are being recorded into
     * @return The records, empty when no transaction is open
     */
    std::span<const std::unique_ptr<UndoRecordBase>> openRecords() const;

    /**
     * @brief Closes the open transaction into a step, dropping it if nothing in it changed anything
     */
    void commit();

    /**
     * @brief Closes the open transaction once no mouse button has been held for a whole update
     * @param mouseButtonHeld Whether a mouse button is held this update
     */
    void commitOnceMouseReleased(bool mouseButtonHeld);

    /**
     * @brief Takes the last step back, unless the history is frozen
     * @return True if a step was taken back
     */
    bool undo();

    /**
     * @brief Makes the last step taken back again, unless the history is frozen
     * @return True if a step was made again
     */
    bool redo();

    /**
     * @brief Sets whether the history is frozen, recording nothing and stepping neither way
     * @param frozen True to freeze
     */
    void setFrozen(bool frozen);

    bool isFrozen() const { return m_frozen; }

  public:
    /**
     * @brief Fires after a step was taken back or made again
     */
    Rapture::EventSignal<void()> onStepApplied;

  private:
    /**
     * @brief Forgets the oldest steps until the history fits its limits again
     */
    void dropStepsPastLimits();

  private:
    UndoHistoryLimits m_limits;
    std::deque<UndoTransaction> m_done;
    std::vector<UndoTransaction> m_undone;
    std::optional<UndoTransaction> m_open;
    size_t m_byteSize = 0;
    bool m_mouseHeldLastUpdate = false;
    bool m_frozen = false;
};

#endif // RAPTURE__UNDO_HISTORY_H
