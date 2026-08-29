#include <gtest/gtest.h>

#include "platform/FileWatcher.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

using namespace Rapture;

namespace {

// the OS reports a change some time after it happens, so a test waits for one rather than assuming
// it has already landed. Long enough not to flake on a loaded machine, and only ever paid on failure.
constexpr std::chrono::milliseconds WAIT_TIMEOUT{4000};
constexpr std::chrono::milliseconds POLL_INTERVAL{10};

class FileWatcherTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_dir = std::filesystem::temp_directory_path() / ("rapture_fw_" + std::to_string(unique));
        std::filesystem::create_directories(m_dir);
    }

    void TearDown() override
    {
        m_watcher.reset();

        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

    void startWatching(bool recursive)
    {
        m_watcher = DirectoryWatcher::create(m_dir, recursive, [this](const FileChange &change) {
            std::lock_guard<std::mutex> lock(m_seenMutex);
            m_seen.push_back(change);
            m_callbackThread = std::this_thread::get_id();
        });
        ASSERT_NE(m_watcher, nullptr);
        ASSERT_TRUE(m_watcher->isValid());
    }

    void writeFile(const std::filesystem::path &path, std::string_view text)
    {
        std::ofstream file(path);
        file << text;
    }

    /**
     * @brief Polls until a change satisfying a predicate arrives, or the timeout runs out
     * @param matches Tested against every change seen so far
     * @return The matching change, or nothing if none arrived in time
     */
    std::optional<FileChange> waitForChange(const std::function<bool(const FileChange &)> &matches)
    {
        auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;

        while (std::chrono::steady_clock::now() < deadline) {
            m_watcher->poll();

            {
                std::lock_guard<std::mutex> lock(m_seenMutex);
                for (const FileChange &change : m_seen) {
                    if (matches(change)) {
                        return change;
                    }
                }
            }

            std::this_thread::sleep_for(POLL_INTERVAL);
        }

        return std::nullopt;
    }

    std::optional<FileChange> waitFor(FileChangeType type, const std::filesystem::path &path)
    {
        return waitForChange([&](const FileChange &change) { return change.type == type && change.path == path; });
    }

    void clearSeen()
    {
        std::lock_guard<std::mutex> lock(m_seenMutex);
        m_seen.clear();
    }

    std::filesystem::path m_dir;
    std::unique_ptr<DirectoryWatcher> m_watcher;

    std::mutex m_seenMutex;
    std::vector<FileChange> m_seen;
    std::thread::id m_callbackThread;
};

TEST_F(FileWatcherTest, ReportsACreatedFile)
{
    startWatching(false);

    std::filesystem::path script = m_dir / "player.lua";
    writeFile(script, "print('hi')");

    EXPECT_TRUE(waitFor(FW_ADDED, script).has_value());
}

TEST_F(FileWatcherTest, ReportsAModifiedFile)
{
    std::filesystem::path script = m_dir / "player.lua";
    writeFile(script, "print('hi')");

    startWatching(false);
    writeFile(script, "print('bye')");

    EXPECT_TRUE(waitFor(FW_MODIFIED, script).has_value());
}

TEST_F(FileWatcherTest, ReportsARemovedFile)
{
    std::filesystem::path script = m_dir / "player.lua";
    writeFile(script, "print('hi')");

    startWatching(false);
    std::filesystem::remove(script);

    EXPECT_TRUE(waitFor(FW_REMOVED, script).has_value());
}

TEST_F(FileWatcherTest, ReportsARenameWithWhereItCameFrom)
{
    std::filesystem::path before = m_dir / "player.lua";
    std::filesystem::path after = m_dir / "enemy.lua";
    writeFile(before, "print('hi')");

    startWatching(false);
    std::filesystem::rename(before, after);

    std::optional<FileChange> change = waitFor(FW_RENAMED, after);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->oldPath, before);
}

TEST_F(FileWatcherTest, RecursiveWatcherSeesANestedFile)
{
    std::filesystem::path nested = m_dir / "scripts";
    std::filesystem::create_directories(nested);

    startWatching(true);

    std::filesystem::path script = nested / "player.lua";
    writeFile(script, "print('hi')");

    EXPECT_TRUE(waitFor(FW_ADDED, script).has_value());
}

TEST_F(FileWatcherTest, RecursiveWatcherSeesAFileInADirectoryMadeAfterItStarted)
{
    startWatching(true);

    std::filesystem::path nested = m_dir / "scripts";
    std::filesystem::create_directories(nested);
    ASSERT_TRUE(waitFor(FW_ADDED, nested).has_value());

    std::filesystem::path script = nested / "player.lua";
    writeFile(script, "print('hi')");

    EXPECT_TRUE(waitFor(FW_ADDED, script).has_value());
}

TEST_F(FileWatcherTest, NonRecursiveWatcherIgnoresANestedFile)
{
    std::filesystem::path nested = m_dir / "scripts";
    std::filesystem::create_directories(nested);

    startWatching(false);

    writeFile(nested / "player.lua", "print('hi')");

    // the nested write has to be given as long to arrive as a change this watcher would report, so
    // a file in the watched directory is written afterwards and its arrival bounds the wait
    std::filesystem::path sibling = m_dir / "sibling.lua";
    writeFile(sibling, "print('hi')");
    ASSERT_TRUE(waitFor(FW_ADDED, sibling).has_value());

    std::lock_guard<std::mutex> lock(m_seenMutex);
    for (const FileChange &change : m_seen) {
        EXPECT_EQ(change.path.parent_path(), m_dir);
    }
}

TEST_F(FileWatcherTest, DispatchesOnThePollingThreadRatherThanItsOwn)
{
    startWatching(false);

    std::filesystem::path script = m_dir / "player.lua";
    writeFile(script, "print('hi')");

    ASSERT_TRUE(waitFor(FW_ADDED, script).has_value());

    std::lock_guard<std::mutex> lock(m_seenMutex);
    EXPECT_EQ(m_callbackThread, std::this_thread::get_id());
}

TEST_F(FileWatcherTest, ReportsNothingUntilPolled)
{
    startWatching(false);

    writeFile(m_dir / "player.lua", "print('hi')");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    {
        std::lock_guard<std::mutex> lock(m_seenMutex);
        EXPECT_TRUE(m_seen.empty());
    }

    EXPECT_TRUE(waitFor(FW_ADDED, m_dir / "player.lua").has_value());
}

TEST_F(FileWatcherTest, WillNotWatchSomethingThatIsNotADirectory)
{
    std::filesystem::path script = m_dir / "player.lua";
    writeFile(script, "print('hi')");

    EXPECT_EQ(DirectoryWatcher::create(script, false, [](const FileChange &) {}), nullptr);
    EXPECT_EQ(DirectoryWatcher::create(m_dir / "nothing_here", false, [](const FileChange &) {}), nullptr);
}

} // namespace
