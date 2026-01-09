#ifndef RP_EDITOR__FILE_EXPLORER_H
#define RP_EDITOR__FILE_EXPLORER_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class FileExplorer {
  public:
    using FileSelectedCallback = std::function<void(const std::filesystem::path &)>;

    FileExplorer();

    void open(const std::filesystem::path &startPath = std::filesystem::current_path(), FileSelectedCallback callback = nullptr);

    void setExtensionFilter(const std::vector<std::string> &extensions);

    bool render();
    bool wasFileSelected() const { return m_wasFileSelected; }
    const std::filesystem::path &getSelectedPath() const { return m_selectedPath; }
    bool isOpen() const { return m_isOpen; }

  private:
    void renderShortcutsPane();
    void renderMainPane();
    void renderNavBar();
    void renderFileList();
    void renderFooter();

    void navigateTo(const std::filesystem::path &path);
    void navigateUp();
    void navigateBack();
    void navigateForward();
    void refreshDirectory();

    bool matchesFilter(const std::filesystem::path &path) const;
    const char *getFileIcon(const std::filesystem::path &path, bool isDirectory) const;

  private:
    static constexpr const char *TITLE = "File Explorer";
    static constexpr const char *MODAL_ID = "FileExplorer##Modal";

    bool m_isOpen = false;
    bool m_wasFileSelected = false;
    bool m_shouldOpenPopup = false;

    std::filesystem::path m_currentPath;
    std::filesystem::path m_selectedPath;

    std::vector<std::string> m_extensionFilter;
    char m_filenameBuffer[256] = "";

    std::vector<std::filesystem::path> m_history;
    int m_historyIndex = -1;

    struct FileEntry {
        std::filesystem::path path;
        std::string name;
        bool isDirectory;
        std::uintmax_t size;
    };
    std::vector<FileEntry> m_entries;
    bool m_needsRefresh = true;

    FileSelectedCallback m_callback;
};

#endif // RP_EDITOR__FILE_EXPLORER_H
