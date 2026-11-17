#include "TextEditorPanel.h"
#include "context/WindowManager.h"

#include <rapture/core/utils/Log.h>

#include <components/ui_scope.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/input/InputCodes.h>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

static constexpr float TOOLBAR_HEIGHT = 28.0f;

static bool s_readFile(const std::filesystem::path &path, std::string &out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

static bool s_writeFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

TextEditorPanel::TextEditorPanel(const WorkspaceContext &context, std::string title) : Panel(std::move(title), context) {}

void TextEditorPanel::build(Amethyst::UIScope &scope)
{
    buildToolbar(scope);

    m_editor = m_root->add<Amethyst::TextArea>();
    m_editor->setBaseProperties(
        {.clipsDescendants = true,
         .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(4.0f), .left = Amethyst::UDim::fromOffset(6.0f)},
         .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, TOOLBAR_HEIGHT),
         .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -TOOLBAR_HEIGHT)});
    m_editor->setSource(&m_buffer);

    m_context.editor->shortcuts->registerShortcut(EDITOR_COMMAND_SAVE, {Rapture::KEY_S, SHORTCUT_MOD_CONTROL}, m_root,
                                                  [this] { save(); });

    refreshStatus();
}

TextEditorPanel::~TextEditorPanel()
{
    // The view outlives this panel when the tab tree is torn down after it, and the buffer it
    // reads from is a member here.
    if (m_root != nullptr && m_editor != nullptr) {
        m_editor->setSource(nullptr);
    }
}

void TextEditorPanel::buildToolbar(Amethyst::UIScope &scope)
{
    scope.frame(
        {.classes = {"panel"}, .base = {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOOLBAR_HEIGHT)}},
        [this](Amethyst::FrameScope &f) {
            f.textButton(
                {.base = {.position = Amethyst::UDim2::fromOffset(4.0f, 3.0f), .size = Amethyst::UDim2::fromOffset(70.0f, 22.0f)},
                 .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
                 .label = "Open..."},
                [this](Amethyst::TextButtonScope &b) {
                    b.component.track(b.component.onMouseButton1ClickCb.connect([this]() {
                        m_context.editor->windows->openFileDialog(FILE_DIALOG_OPEN,
                                                                  [this](const std::filesystem::path &path) { openFile(path); });
                    }));
                });

            f.textLabel(
                {.base = {.position = Amethyst::UDim2(0.0f, 80.0f, 0.0f, 3.0f), .size = Amethyst::UDim2(1.0f, -84.0f, 0.0f, 22.0f)},
                 .style = {.backgroundTransparency = 1.0f},
                 .text = {.textXAlignment = Amethyst::TextXAlignment::LEFT, .textYAlignment = Amethyst::TextYAlignment::CENTER},
                 .label = ""},
                [this](Amethyst::TextLabelScope &t) { m_status = &t.component; });
        });
}

void TextEditorPanel::loadText(const std::string &text)
{
    m_buffer.load(text);
    m_savedRevision = m_buffer.revision();

    if (m_editor != nullptr) {
        m_editor->setCursorPosition({0, 0});
        m_editor->scrollToLine(0);
    }
    refreshStatus();
}

bool TextEditorPanel::openFile(const std::filesystem::path &path)
{
    std::string contents;
    if (!s_readFile(path, contents)) {
        RP_ERROR("Could not read {}", path.string());
        return false;
    }

    m_path = path;
    loadText(contents);
    return true;
}

bool TextEditorPanel::openScript(Rapture::AssetHandle handle)
{
    const std::filesystem::path source = Rapture::AssetManager::getAssetMetadata(handle).getSourcePath();
    if (source.empty()) {
        RP_ERROR("script {} names no Lua file", handle);
        return false;
    }

    return openFile(source);
}

bool TextEditorPanel::save()
{
    if (m_path.empty()) {
        RP_WARN("Nothing to save, no file is open");
        return false;
    }

    if (!s_writeFile(m_path, m_buffer.text())) {
        RP_ERROR("Could not write {}", m_path.string());
        return false;
    }

    m_savedRevision = m_buffer.revision();
    refreshStatus();
    return true;
}

void TextEditorPanel::refreshStatus()
{
    if (m_status == nullptr) {
        return;
    }

    std::string label;
    if (m_path.empty()) {
        label = "No file";
    } else {
        label = m_path.filename().string();
    }

    if (isModified()) {
        label += " *";
    }

    if (m_editor != nullptr) {
        Amethyst::TextPosition cursor = m_editor->getCursorPosition();
        label += "    Ln " + std::to_string(cursor.line + 1) + ", Col " + std::to_string(cursor.column + 1);
        label += "    " + std::to_string(m_buffer.lineCount()) + " lines";
    }

    m_status->setText(label);
}

void TextEditorPanel::onUpdate(float dt)
{
    (void)dt;
    if (m_editor == nullptr) {
        return;
    }

    Amethyst::TextPosition cursor = m_editor->getCursorPosition();
    if (cursor == m_shownCursor && m_buffer.revision() == m_shownRevision) {
        return;
    }

    m_shownCursor = cursor;
    m_shownRevision = m_buffer.revision();
    refreshStatus();
}
