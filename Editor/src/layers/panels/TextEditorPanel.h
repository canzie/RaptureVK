#ifndef RAPTURE__TEXT_EDITOR_PANEL_H
#define RAPTURE__TEXT_EDITOR_PANEL_H

#include "layers/panels/Panel.h"
#include "text/TextBuffer.h"

#include <amethyst/Amethyst.h>
#include <components/text_area.h>

#include <cstdint>
#include <filesystem>

/**
 * @brief Edits one text file, held in a TextBuffer and shown through a TextArea.
 */
class TextEditorPanel : public Panel {
  public:
    TextEditorPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, std::string title = "Text Editor");
    ~TextEditorPanel() override;

    void onUpdate(float dt) override;

    /**
     * @brief Read a file into the editor, replacing whatever it holds.
     * @param path File to read
     * @return Whether the file could be read
     */
    bool openFile(const std::filesystem::path &path);

    /**
     * @brief Write the contents back to the file they were read from.
     * @return Whether the file could be written
     */
    bool save();

    const std::filesystem::path &getPath() const { return m_path; }

    /**
     * @brief Whether there are edits that have not been written to the file.
     */
    bool isModified() const { return m_buffer.revision() != m_savedRevision; }

  private:
    void buildToolbar(Amethyst::UIScope &scope);
    void refreshStatus();

  private:
    TextBuffer m_buffer;
    std::filesystem::path m_path;
    uint64_t m_savedRevision = 0;

    Amethyst::TextArea *m_editor = nullptr;
    Amethyst::TextLabel *m_status = nullptr;

    Amethyst::TextPosition m_shownCursor{UINT64_MAX, UINT64_MAX};
    uint64_t m_shownRevision = UINT64_MAX;
};

#endif // RAPTURE__TEXT_EDITOR_PANEL_H
