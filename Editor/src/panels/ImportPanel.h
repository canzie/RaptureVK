#ifndef RAPTURE__IMPORT_PANEL_H
#define RAPTURE__IMPORT_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <components/window.h>

#include <filesystem>
#include <functional>

class ImportPanel {
  public:
    ImportPanel(Amethyst::Window &window, const std::filesystem::path &path, const std::filesystem::path &outputFolder);
    ~ImportPanel();
    ImportPanel(const ImportPanel &) = delete;
    ImportPanel &operator=(const ImportPanel &) = delete;
    ImportPanel(ImportPanel &&) = delete;
    ImportPanel &operator=(ImportPanel &&) = delete;

    /**
     * @brief Fired after the panel has closed, for a caller awaiting the result.
     */
    std::function<void(void)> onClose;

  private:
    void build(void);
    void doImport(void);
    void close(void);

    std::filesystem::path m_path;
    std::filesystem::path m_outputFolder;

    Amethyst::Popup *m_popup = nullptr;
    Amethyst::Frame *m_titleBar = nullptr;
    Amethyst::TextInput *m_outputInput = nullptr;
    Amethyst::EventConnection m_popupDestroyConn;

    Amethyst::TickHandle m_tick;
    bool m_importRequested = false;
    bool m_closeRequested = false;
    bool m_createSubfolder = true;
};

#endif // RAPTURE__IMPORT_PANEL_H
