#ifdef _WIN32

#include "FileWatcher.h"

#include "logging/Log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <thread>

namespace Rapture {

static constexpr DWORD s_notifyFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

class Win32DirectoryWatcher : public DirectoryWatcher {
  public:
    Win32DirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback);
    ~Win32DirectoryWatcher() override;

  private:
    void run();
    void processEvents(const char *buffer, DWORD length);

    HANDLE m_dirHandle = INVALID_HANDLE_VALUE;
    HANDLE m_ioEvent = nullptr;
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;
};

Win32DirectoryWatcher::Win32DirectoryWatcher(std::filesystem::path directory, bool recursive, Callback callback)
    : DirectoryWatcher(std::move(directory), recursive, std::move(callback))
{
    if (!std::filesystem::is_directory(m_directory)) {
        RP_CORE_ERROR("cannot watch '{}': not a directory", m_directory.string());
        return;
    }

    m_dirHandle = CreateFileW(
        m_directory.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (m_dirHandle == INVALID_HANDLE_VALUE) {
        RP_CORE_ERROR("CreateFileW failed for '{}'", m_directory.string());
        return;
    }

    m_ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_ioEvent == nullptr || m_stopEvent == nullptr) {
        RP_CORE_ERROR("CreateEventW failed for '{}'", m_directory.string());
        if (m_ioEvent != nullptr) {
            CloseHandle(m_ioEvent);
        }
        if (m_stopEvent != nullptr) {
            CloseHandle(m_stopEvent);
        }
        CloseHandle(m_dirHandle);
        m_dirHandle = INVALID_HANDLE_VALUE;
        m_ioEvent = nullptr;
        m_stopEvent = nullptr;
        return;
    }

    m_valid = true;
    m_thread = std::thread(&Win32DirectoryWatcher::run, this);
}

Win32DirectoryWatcher::~Win32DirectoryWatcher()
{
    if (m_thread.joinable()) {
        SetEvent(m_stopEvent);
        CancelIoEx(m_dirHandle, nullptr);
        m_thread.join();
    }

    if (m_ioEvent != nullptr) {
        CloseHandle(m_ioEvent);
    }
    if (m_stopEvent != nullptr) {
        CloseHandle(m_stopEvent);
    }
    if (m_dirHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_dirHandle);
    }
}

void Win32DirectoryWatcher::run()
{
    alignas(DWORD) char buffer[64 * 1024];

    OVERLAPPED overlapped = {};
    overlapped.hEvent = m_ioEvent;

    HANDLE waitHandles[2] = {m_ioEvent, m_stopEvent};

    while (true) {
        DWORD bytesReturned = 0;
        BOOL ok = ReadDirectoryChangesW(
            m_dirHandle,
            buffer,
            sizeof(buffer),
            m_recursive ? TRUE : FALSE,
            s_notifyFilter,
            &bytesReturned,
            &overlapped,
            nullptr);
        if (ok == FALSE) {
            RP_CORE_ERROR("ReadDirectoryChangesW failed for '{}'", m_directory.string());
            break;
        }

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            break;
        }

        DWORD transferred = 0;
        if (GetOverlappedResult(m_dirHandle, &overlapped, &transferred, FALSE) == FALSE || transferred == 0) {
            ResetEvent(m_ioEvent);
            continue;
        }
        ResetEvent(m_ioEvent);

        processEvents(buffer, transferred);
    }
}

void Win32DirectoryWatcher::processEvents(const char *buffer, DWORD length)
{
    std::filesystem::path renamedOld;

    DWORD offset = 0;
    while (offset < length) {
        const FILE_NOTIFY_INFORMATION *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer + offset);

        std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
        std::filesystem::path fullPath = m_directory / name;

        switch (info->Action) {
        case FILE_ACTION_ADDED:
            enqueue({FW_ADDED, fullPath, {}});
            break;
        case FILE_ACTION_REMOVED:
            enqueue({FW_REMOVED, fullPath, {}});
            break;
        case FILE_ACTION_MODIFIED:
            enqueue({FW_MODIFIED, fullPath, {}});
            break;
        case FILE_ACTION_RENAMED_OLD_NAME:
            renamedOld = fullPath;
            break;
        case FILE_ACTION_RENAMED_NEW_NAME:
            enqueue({FW_RENAMED, fullPath, renamedOld});
            renamedOld.clear();
            break;
        default:
            break;
        }

        if (info->NextEntryOffset == 0) {
            break;
        }
        offset += info->NextEntryOffset;
    }
}

std::unique_ptr<DirectoryWatcher> DirectoryWatcher::create(const std::filesystem::path &directory, bool recursive, Callback callback)
{
    auto watcher = std::make_unique<Win32DirectoryWatcher>(directory, recursive, std::move(callback));
    if (!watcher->isValid()) {
        return nullptr;
    }
    return watcher;
}

} // namespace Rapture

#endif // _WIN32
