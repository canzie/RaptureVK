#ifndef RAPTURE__SCRATCH_BUFFER_H
#define RAPTURE__SCRATCH_BUFFER_H

#include "Logging/Log.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class ScratchBuffer {
  public:
    ScratchBuffer(size_t initialSize = 4096) : m_buffer(initialSize), m_offset(0) {}

    void *allocate(size_t size, size_t alignment = alignof(std::max_align_t))
    {
        m_offset = (m_offset + alignment - 1) & ~(alignment - 1);

        size_t required = m_offset + size;
        if (required > m_buffer.size()) {
            size_t oldSize = m_buffer.size();
            m_buffer.resize(required * 2);
            Rapture::RP_WARN("ScratchBuffer resized from {} to {} bytes", oldSize, m_buffer.size());
        }

        void *ptr = m_buffer.data() + m_offset;
        m_offset += size;
        return ptr;
    }

    void reset() { m_offset = 0; }

    size_t getCurrentUsage() const { return m_offset; }
    size_t getCapacity() const { return m_buffer.size(); }

  private:
    std::vector<uint8_t> m_buffer;
    size_t m_offset;
};

#endif // RAPTURE__SCRATCH_BUFFER_H
