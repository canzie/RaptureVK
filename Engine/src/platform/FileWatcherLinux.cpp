#ifdef __linux__

#include "FileWatcher.h"

#include "core/utils/Log.h"

#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>

#include <thread>
#include <unordered_map>

namespace Rapture {

static constexpr uint32_t s_watchMask =
    IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;

class InotifyDirectoryWatcher : public DirectoryWatcher {
  public:
    InotifyDirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback);
    ~InotifyDirectoryWatcher() override;

  private:
    void run();
    void addWatch(const std::filesystem::path &directory);
    void processEvents(const char *buffer, ssize_t length);

    int m_inotifyFd = -1;
    int m_stopFd = -1;
    std::thread m_thread;

    std::unordered_map<int, std::filesystem::path> m_watchPaths;
};

InotifyDirectoryWatcher::InotifyDirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback)
    : DirectoryWatcher(std::move(directory), recursive, std::move(callback))
{
    if (!std::filesystem::is_directory(m_directory)) {
        RP_CORE_ERROR("cannot watch '{}': not a directory", m_directory.string());
        return;
    }

    m_inotifyFd = inotify_init1(IN_NONBLOCK);
    if (m_inotifyFd < 0) {
        RP_CORE_ERROR("inotify_init1 failed for '{}'", m_directory.string());
        return;
    }

    m_stopFd = eventfd(0, EFD_NONBLOCK);
    if (m_stopFd < 0) {
        RP_CORE_ERROR("eventfd failed for '{}'", m_directory.string());
        close(m_inotifyFd);
        m_inotifyFd = -1;
        return;
    }

    addWatch(m_directory);
    if (m_recursive) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_directory, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            if (it->is_directory()) {
                addWatch(it->path());
            }
        }
    }

    if (m_watchPaths.empty()) {
        RP_CORE_ERROR("failed to add any inotify watch for '{}'", m_directory.string());
        close(m_stopFd);
        close(m_inotifyFd);
        m_stopFd = -1;
        m_inotifyFd = -1;
        return;
    }

    m_valid = true;
    m_thread = std::thread(&InotifyDirectoryWatcher::run, this);
}

InotifyDirectoryWatcher::~InotifyDirectoryWatcher()
{
    if (m_thread.joinable()) {
        uint64_t one = 1;
        ssize_t written = write(m_stopFd, &one, sizeof(one));
        (void)written;
        m_thread.join();
    }

    if (m_stopFd >= 0) {
        close(m_stopFd);
    }
    if (m_inotifyFd >= 0) {
        close(m_inotifyFd);
    }
}

void InotifyDirectoryWatcher::addWatch(const std::filesystem::path &directory)
{
    int wd = inotify_add_watch(m_inotifyFd, directory.c_str(), s_watchMask);
    if (wd < 0) {
        RP_CORE_WARN("inotify_add_watch failed for '{}'", directory.string());
        return;
    }
    m_watchPaths[wd] = directory;
}

void InotifyDirectoryWatcher::run()
{
    alignas(struct inotify_event) char buffer[64 * 1024];

    pollfd fds[2];
    fds[0] = {m_inotifyFd, POLLIN, 0};
    fds[1] = {m_stopFd, POLLIN, 0};

    while (true) {
        int ready = ::poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            RP_CORE_ERROR("poll failed on inotify watcher for '{}'", m_directory.string());
            break;
        }

        if ((fds[1].revents & POLLIN) != 0) {
            break;
        }

        if ((fds[0].revents & POLLIN) == 0) {
            continue;
        }

        ssize_t length = read(m_inotifyFd, buffer, sizeof(buffer));
        if (length <= 0) {
            continue;
        }
        processEvents(buffer, length);
    }
}

void InotifyDirectoryWatcher::processEvents(const char *buffer, ssize_t length)
{
    std::unordered_map<uint32_t, std::filesystem::path> movedFrom;

    ssize_t offset = 0;
    while (offset < length) {
        const inotify_event *event = reinterpret_cast<const inotify_event *>(buffer + offset);
        offset += sizeof(inotify_event) + event->len;

        auto watchIt = m_watchPaths.find(event->wd);
        if (watchIt == m_watchPaths.end()) {
            continue;
        }

        bool isDir = (event->mask & IN_ISDIR) != 0;
        std::filesystem::path fullPath = watchIt->second;
        if (event->len > 0) {
            fullPath /= event->name;
        }

        if ((event->mask & IN_CREATE) != 0) {
            if (isDir && m_recursive) {
                addWatch(fullPath);
            }
            enqueue({FW_ADDED, fullPath, {}});
        } else if ((event->mask & IN_CLOSE_WRITE) != 0) {
            enqueue({FW_MODIFIED, fullPath, {}});
        } else if ((event->mask & IN_DELETE) != 0) {
            enqueue({FW_REMOVED, fullPath, {}});
        } else if ((event->mask & IN_DELETE_SELF) != 0) {
            m_watchPaths.erase(watchIt);
        } else if ((event->mask & IN_MOVED_FROM) != 0) {
            movedFrom[event->cookie] = fullPath;
        } else if ((event->mask & IN_MOVED_TO) != 0) {
            auto fromIt = movedFrom.find(event->cookie);
            if (fromIt != movedFrom.end()) {
                enqueue({FW_RENAMED, fullPath, fromIt->second});
                movedFrom.erase(fromIt);
            } else {
                enqueue({FW_ADDED, fullPath, {}});
            }
            if (isDir && m_recursive) {
                addWatch(fullPath);
            }
        }
    }

    for (auto &[cookie, path] : movedFrom) {
        enqueue({FW_REMOVED, path, {}});
    }
}

std::unique_ptr<DirectoryWatcher> DirectoryWatcher::create(const std::filesystem::path &directory, bool recursive, Callback callback)
{
    auto watcher = std::make_unique<InotifyDirectoryWatcher>(directory, recursive, std::move(callback));
    if (!watcher->isValid()) {
        return nullptr;
    }
    return watcher;
}

} // namespace Rapture

#endif // __linux__
