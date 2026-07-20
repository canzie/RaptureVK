#ifndef RAPTURE__FILE_WATCHER_H
#define RAPTURE__FILE_WATCHER_H

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Rapture {

enum FileChangeType {
    FW_ADDED,
    FW_REMOVED,
    FW_MODIFIED,
    FW_RENAMED,
    FW_COUNT
};

struct FileChange {
    FileChangeType type;
    std::filesystem::path path;
    std::filesystem::path oldPath;
};

/**
 * @brief Watches a directory for filesystem changes.
 *
 * A platform subclass runs a background thread that collects raw OS events and
 * calls enqueue(). Queued changes are dispatched on the thread that calls
 * poll(), so the callback never fires from the internal watcher thread.
 */
class DirectoryWatcher {
  public:
    using Callback = std::function<void(const FileChange &)>;

    /**
     * @brief Creates a platform directory watcher.
     * @param directory The directory to watch
     * @param recursive Whether to watch nested subdirectories
     * @param callback Invoked once per change during poll()
     * @return The watcher, or nullptr if the directory cannot be watched
     */
    static std::unique_ptr<DirectoryWatcher> create(const std::filesystem::path &directory, bool recursive, Callback callback);

    virtual ~DirectoryWatcher() = default;

    /**
     * @brief Dispatches all pending changes on the calling thread.
     */
    void poll();

    /**
     * @brief Whether the watcher is running and observing the directory.
     * @return True if the watch was set up successfully
     */
    bool isValid() const { return m_valid; }

    /**
     * @brief The directory being watched.
     * @return The watched directory path
     */
    const std::filesystem::path &getDirectory() const { return m_directory; }

  protected:
    DirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback);

    /**
     * @brief Buffers a change to be dispatched on the next poll().
     * @param change The change to queue
     */
    void enqueue(FileChange change);

    std::filesystem::path m_directory;
    bool m_recursive;
    bool m_valid = false;

  private:
    Callback m_callback;

    std::mutex m_queueMutex;
    std::vector<FileChange> m_pending;
};

} // namespace Rapture

#endif // RAPTURE__FILE_WATCHER_H
