#include "UndoHistory.h"

#include <utility>

UndoHistory::UndoHistory(UndoHistoryLimits limits) : m_limits(limits) {}

void UndoHistory::record(std::unique_ptr<UndoRecordBase> record)
{
    if (m_frozen) {
        return;
    }

    if (!m_open.has_value()) {
        m_open.emplace();
    }
    m_open->records.push_back(std::move(record));
}

std::span<const std::unique_ptr<UndoRecordBase>> UndoHistory::openRecords() const
{
    if (!m_open.has_value()) {
        return {};
    }
    return m_open->records;
}

void UndoHistory::commit()
{
    if (!m_open.has_value()) {
        return;
    }

    UndoTransaction transaction = std::move(*m_open);
    m_open.reset();

    std::erase_if(transaction.records, [](std::unique_ptr<UndoRecordBase> &record) {
        record->finish();
        return record->isNoOp();
    });

    if (transaction.records.empty()) {
        return;
    }

    for (const auto &record : transaction.records) {
        transaction.byteSize += record->byteSize();
    }

    for (const UndoTransaction &undone : m_undone) {
        m_byteSize -= undone.byteSize;
    }
    m_undone.clear();

    m_byteSize += transaction.byteSize;
    m_done.push_back(std::move(transaction));

    dropStepsPastLimits();
}

void UndoHistory::commitOnceMouseReleased(bool mouseButtonHeld)
{
    // a release is only taken once the update it happened in is over, since whatever the button was
    // driving still applies its last change during that update
    if (!mouseButtonHeld && !m_mouseHeldLastUpdate) {
        commit();
    }
    m_mouseHeldLastUpdate = mouseButtonHeld;
}

bool UndoHistory::undo()
{
    commit();

    if (m_frozen || m_done.empty()) {
        return false;
    }

    UndoTransaction transaction = std::move(m_done.back());
    m_done.pop_back();

    for (size_t i = transaction.records.size(); i > 0; i--) {
        transaction.records[i - 1]->undo();
    }

    m_undone.push_back(std::move(transaction));
    onStepApplied.fire();
    return true;
}

bool UndoHistory::redo()
{
    commit();

    if (m_frozen || m_undone.empty()) {
        return false;
    }

    UndoTransaction transaction = std::move(m_undone.back());
    m_undone.pop_back();

    for (const auto &record : transaction.records) {
        record->redo();
    }

    m_done.push_back(std::move(transaction));
    onStepApplied.fire();
    return true;
}

void UndoHistory::setFrozen(bool frozen)
{
    if (frozen) {
        commit();
    }
    m_frozen = frozen;
}

void UndoHistory::dropStepsPastLimits()
{
    while (!m_done.empty() && (m_done.size() > m_limits.maxSteps || m_byteSize > m_limits.maxBytes)) {
        m_byteSize -= m_done.front().byteSize;
        m_done.pop_front();
    }
}
