#ifndef RAPTURE__FILE_BROWSER_H
#define RAPTURE__FILE_BROWSER_H

#include <amethyst/Amethyst.h>
#include <components/table.h>
#include <components/ui_scope.h>
#include <components/window.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief File open/save dialog laid out in Amethyst.
 */
class FileBrowser {
  public:
    /**
     * @brief Whether the dialog opens an existing file or names a new one.
     */
    enum class Mode {
        OPEN, ///< read-only filename field showing the current selection
        SAVE  ///< editable filename field for naming a new file
    };

    explicit FileBrowser(Amethyst::Instance &parent, Mode mode = Mode::OPEN);
    ~FileBrowser();
    FileBrowser(const FileBrowser &) = delete;
    FileBrowser &operator=(const FileBrowser &) = delete;
    FileBrowser(FileBrowser &&) = delete;
    FileBrowser &operator=(FileBrowser &&) = delete;

    /**
     * @brief Invoked when the dialog should be dismissed (cancel, or after a confirm).
     */
    std::function<void(void)> onClose;

    /**
     * @brief Invoked with the chosen path when the user confirms a file.
     */
    std::function<void(const std::filesystem::path &)> onConfirm;

  private:
    struct NavButton {
        Amethyst::Frame *surface = nullptr;
        Amethyst::ImageLabel *icon = nullptr;
        bool enabled = true;
    };

    struct Entry {
        std::filesystem::path path;
        bool isDir = false;
    };

    void buildContent(void);
    void setupTopBar(void);
    void setupSideBar(void);
    void setupListArea(void);
    void setupStatusBar(void);
    void setupFooter(void);

    void buildNavButton(Amethyst::FrameScope &slot, const char *svg, std::function<void(void)> onClick, NavButton *store);
    void applyNavEnabled(NavButton &nav, bool enabled);

    void readDirectory(void);
    void populate(void);
    void processDeferred(void);
    void navigateTo(const std::filesystem::path &dir, bool recordHistory = true);
    void goBack(void);
    void goForward(void);
    void goUp(void);
    void refresh(void);
    void updateNavState(void);

    void onRowClicked(uint32_t row);
    void updateSelectionLabel(void);
    void confirm(const std::filesystem::path &path);

    Mode m_mode;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::EventConnection m_rootDestroyConn;

    Amethyst::TextInput *m_pathInput = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;

    Amethyst::Table *m_table = nullptr;
    Amethyst::TextLabel *m_statusLabel = nullptr;
    Amethyst::TextLabel *m_selectionLabel = nullptr;
    Amethyst::TextInput *m_filenameInput = nullptr;

    NavButton m_backButton;
    NavButton m_forwardButton;

    std::filesystem::path m_currentDirectory;
    std::vector<Entry> m_allEntries;
    std::vector<Entry> m_visibleEntries;
    std::vector<std::filesystem::path> m_backStack;
    std::vector<std::filesystem::path> m_forwardStack;

    std::string m_searchText;
    std::vector<std::string> m_extensionFilter;
    int m_selectedRow = -1;

    Amethyst::TickHandle m_tick;
    std::optional<std::filesystem::path> m_pendingNavigation;
};

#endif // RAPTURE__FILE_BROWSER_H
