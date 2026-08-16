#include "FileWatcher.h"

namespace Rapture {

DirectoryWatcher::DirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback)
    : m_directory(std::move(directory)), m_recursive(recursive), m_callback(std::move(callback))
{
}

void DirectoryWatcher::enqueue(FileChange change)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_pending.push_back(std::move(change));
}

void DirectoryWatcher::poll()
{
    std::vector<FileChange> drained;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        drained.swap(m_pending);
    }

    if (!m_callback) {
        return;
    }

    for (const FileChange &change : drained) {
        m_callback(change);
    }
}

} // namespace Rapture
