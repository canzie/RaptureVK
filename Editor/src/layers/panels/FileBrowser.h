#ifndef RAPTURE__FILE_BROWSER_H
#define RAPTURE__FILE_BROWSER_H

#include <amethyst/Amethyst.h>
#include <components/table.h>

#include <filesystem>

/**
 * @brief File open/save dialog laid out in Amethyst.
 *
 * Builds a root frame filling the supplied parent. Layout only for now: the
 * top bar, sidebar bookmarks, sortable list header, file rows, status bar and
 * footer are all wired visually but navigation/sorting are not yet hooked up.
 */
class FileBrowser {
  public:
    explicit FileBrowser(Amethyst::Instance &parent);
    ~FileBrowser();
    FileBrowser(const FileBrowser &) = delete;
    FileBrowser &operator=(const FileBrowser &) = delete;
    FileBrowser(FileBrowser &&) = delete;
    FileBrowser &operator=(FileBrowser &&) = delete;

  private:
    void buildContent();
    void setupTopBar();
    void setupSideBar();
    void setupListArea();
    void setupStatusBar();
    void setupFooter();

    void populate();

    Amethyst::Frame *m_root = nullptr;
    Amethyst::EventConnection m_rootDestroyConn;

    Amethyst::TextInput *m_pathInput = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;

    Amethyst::Table *m_table = nullptr;
    Amethyst::TextLabel *m_statusLabel = nullptr;
    Amethyst::TextLabel *m_selectionLabel = nullptr;
    Amethyst::TextInput *m_filenameInput = nullptr;

    std::filesystem::path m_currentDirectory;
};

#endif // RAPTURE__FILE_BROWSER_H
