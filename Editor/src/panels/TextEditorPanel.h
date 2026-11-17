#ifndef RAPTURE__TEXT_EDITOR_PANEL_H
#define RAPTURE__TEXT_EDITOR_PANEL_H

#include "panels/Panel.h"
#include "text/TextBuffer.h"

#include <amethyst/Amethyst.h>
#include <components/text_area.h>

#include <rapture/assets/asset_manager/AssetCommon.h>

#include <cstdint>
#include <filesystem>

/**
 * @brief Edits one text file, held in a TextBuffer and shown through a TextArea.
 */
class TextEditorPanel : public Panel {
  public:
    explicit TextEditorPanel(const WorkspaceContext &context, std::string title = "Text Editor");
    ~TextEditorPanel() override;

    void onUpdate(float dt) override;

    /**
     * @brief Read a file into the editor, replacing whatever it holds.
     * @param path File to read
     * @return Whether the file could be read
     */
    bool openFile(const std::filesystem::path &path);

    /**
     * @brief Read a script's Lua file into the editor, replacing whatever it holds.
     * @param handle The script asset whose Lua to edit
     * @return Whether the Lua file could be read
     */
    bool openScript(Rapture::AssetHandle handle);

    /**
     * @brief Write the contents back to where they were read from.
     * @return Whether they could be written
     */
    bool save();

    const std::filesystem::path &getPath() const { return m_path; }

    /**
     * @brief Whether there are edits that have not been written back.
     */
    bool isModified() const { return m_buffer.revision() != m_savedRevision; }

  protected:
    void build(Amethyst::UIScope &scope) override;

  private:
    void buildToolbar(Amethyst::UIScope &scope);
    void refreshStatus();

    /**
     * @brief Shows text in the editor as the whole of what is open, with the cursor back at the top
     * @param text The text to show
     */
    void loadText(const std::string &text);

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
