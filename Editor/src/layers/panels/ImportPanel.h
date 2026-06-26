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
    ImportPanel(Amethyst::Window &window, const std::filesystem::path &path);
    ~ImportPanel();
    ImportPanel(const ImportPanel &) = delete;
    ImportPanel &operator=(const ImportPanel &) = delete;
    ImportPanel(ImportPanel &&) = delete;
    ImportPanel &operator=(ImportPanel &&) = delete;

    /**
     * @brief Fired after the panel has closed, for a caller awaiting the result.
     */
    std::function<void()> onClose;

  private:
    void build();
    void doImport();
    void close();

    std::filesystem::path m_path;

    Amethyst::Popup *m_popup = nullptr;
    Amethyst::Frame *m_titleBar = nullptr;
    Amethyst::EventConnection m_popupDestroyConn;

    Amethyst::TickHandle m_tick;
    bool m_importRequested = false;
    bool m_closeRequested = false;
};

#endif // RAPTURE__IMPORT_PANEL_H
