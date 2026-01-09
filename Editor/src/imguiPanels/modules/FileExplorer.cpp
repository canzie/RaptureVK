#include "FileExplorer.h"

#include "imguiPanels/IconsMaterialDesign.h"
#include "imguiPanels/themes/imguiPanelStyle.h"

#include <imgui.h>

#include <algorithm>

FileExplorer::FileExplorer() {}

void FileExplorer::open(const std::filesystem::path &startPath, FileSelectedCallback callback)
{
    m_currentPath = std::filesystem::absolute(startPath);
    m_callback = callback;
    m_wasFileSelected = false;
    m_filenameBuffer[0] = '\0';
    m_selectedPath.clear();

    m_history.clear();
    m_history.push_back(m_currentPath);
    m_historyIndex = 0;

    m_needsRefresh = true;
    m_shouldOpenPopup = true;
    m_isOpen = true;
}

void FileExplorer::setExtensionFilter(const std::vector<std::string> &extensions)
{
    m_extensionFilter = extensions;
    for (auto &ext : m_extensionFilter) {
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
}

bool FileExplorer::render()
{
    if (!m_isOpen) {
        return false;
    }

    if (m_shouldOpenPopup) {
        ImGui::OpenPopup(MODAL_ID);
        m_shouldOpenPopup = false;
    }

    ImVec2 modalSize(800.0f, 500.0f);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_FirstUseEver);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool closed = false;
    if (ImGui::BeginPopupModal(MODAL_ID, &m_isOpen, ImGuiWindowFlags_NoScrollbar)) {
        if (m_needsRefresh) {
            refreshDirectory();
            m_needsRefresh = false;
        }

        float shortcutsPaneWidth = 150.0f;

        ImGui::BeginChild("ShortcutsPane", ImVec2(shortcutsPaneWidth, -ImGui::GetFrameHeightWithSpacing() - 30.0f),
                          ImGuiChildFlags_Borders);
        renderShortcutsPane();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("MainPane", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 30.0f), ImGuiChildFlags_Borders);
        renderMainPane();
        ImGui::EndChild();

        renderFooter();

        ImGui::EndPopup();
    }

    if (!m_isOpen) {
        closed = true;
    }

    return closed;
}

void FileExplorer::renderShortcutsPane()
{
    ImGui::TextColored(ColorPalette::TEXT_MUTED, "Shortcuts");
    ImGui::Separator();
}

void FileExplorer::renderMainPane()
{
    renderNavBar();
    ImGui::Separator();
    renderFileList();
}

void FileExplorer::renderNavBar()
{
    bool canGoBack = m_historyIndex > 0;
    bool canGoForward = m_historyIndex < static_cast<int>(m_history.size()) - 1;

    ImGui::BeginDisabled(!canGoBack);
    if (ImGui::Button(ICON_MD_ARROW_BACK)) {
        navigateBack();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!canGoForward);
    if (ImGui::Button(ICON_MD_ARROW_FORWARD)) {
        navigateForward();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_ARROW_UPWARD)) {
        navigateUp();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MD_REFRESH)) {
        m_needsRefresh = true;
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(-1);
    std::string pathStr = m_currentPath.string();
    char pathBuffer[512];
    strncpy(pathBuffer, pathStr.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    if (ImGui::InputText("##PathBar", pathBuffer, sizeof(pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::filesystem::path newPath(pathBuffer);
        if (std::filesystem::exists(newPath) && std::filesystem::is_directory(newPath)) {
            navigateTo(newPath);
        }
    }
}

void FileExplorer::renderFileList()
{
    ImGui::BeginChild("FileList", ImVec2(0, 0), ImGuiChildFlags_None);

    for (const auto &entry : m_entries) {
        const char *icon = getFileIcon(entry.path, entry.isDirectory);

        bool isSelected = (m_selectedPath == entry.path);

        std::string label = std::string(icon) + " " + entry.name;

        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (entry.isDirectory) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    navigateTo(entry.path);
                }
            } else {
                m_selectedPath = entry.path;
                strncpy(m_filenameBuffer, entry.name.c_str(), sizeof(m_filenameBuffer) - 1);
                m_filenameBuffer[sizeof(m_filenameBuffer) - 1] = '\0';

                if (ImGui::IsMouseDoubleClicked(0)) {
                    m_wasFileSelected = true;
                    if (m_callback) {
                        m_callback(m_selectedPath);
                    }
                    m_isOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }

    ImGui::EndChild();
}

void FileExplorer::renderFooter()
{
    ImGui::Text("File:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170.0f);
    ImGui::InputText("##Filename", m_filenameBuffer, sizeof(m_filenameBuffer));

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
        m_wasFileSelected = false;
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    bool hasSelection = m_filenameBuffer[0] != '\0';
    ImGui::BeginDisabled(!hasSelection);

    ImGui::PushStyleColor(ImGuiCol_Button, ColorPalette::ACCENT_PRIMARY);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColorPalette::ACCENT_HOVER);

    if (ImGui::Button("Open", ImVec2(80, 0))) {
        if (!m_selectedPath.empty()) {
            m_wasFileSelected = true;
            if (m_callback) {
                m_callback(m_selectedPath);
            }
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
}

void FileExplorer::navigateTo(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        return;
    }

    m_currentPath = std::filesystem::absolute(path);

    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    }
    m_history.push_back(m_currentPath);
    m_historyIndex = static_cast<int>(m_history.size()) - 1;

    m_needsRefresh = true;
    m_selectedPath.clear();
    m_filenameBuffer[0] = '\0';
}

void FileExplorer::navigateUp()
{
    if (m_currentPath.has_parent_path() && m_currentPath.parent_path() != m_currentPath) {
        navigateTo(m_currentPath.parent_path());
    }
}

void FileExplorer::navigateBack()
{
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_currentPath = m_history[m_historyIndex];
        m_needsRefresh = true;
        m_selectedPath.clear();
        m_filenameBuffer[0] = '\0';
    }
}

void FileExplorer::navigateForward()
{
    if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
        m_historyIndex++;
        m_currentPath = m_history[m_historyIndex];
        m_needsRefresh = true;
        m_selectedPath.clear();
        m_filenameBuffer[0] = '\0';
    }
}

void FileExplorer::refreshDirectory()
{
    m_entries.clear();

    if (!std::filesystem::exists(m_currentPath)) {
        return;
    }

    std::vector<FileEntry> directories;
    std::vector<FileEntry> files;

    for (const auto &entry : std::filesystem::directory_iterator(m_currentPath)) {
        FileEntry fe;
        fe.path = entry.path();
        fe.name = entry.path().filename().string();
        fe.isDirectory = entry.is_directory();
        fe.size = fe.isDirectory ? 0 : entry.file_size();

        if (fe.isDirectory) {
            directories.push_back(std::move(fe));
        } else if (matchesFilter(fe.path)) {
            files.push_back(std::move(fe));
        }
    }

    auto sortByName = [](const FileEntry &a, const FileEntry &b) { return a.name < b.name; };

    std::sort(directories.begin(), directories.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);

    m_entries.reserve(directories.size() + files.size());
    for (auto &d : directories) {
        m_entries.push_back(std::move(d));
    }
    for (auto &f : files) {
        m_entries.push_back(std::move(f));
    }
}

bool FileExplorer::matchesFilter(const std::filesystem::path &path) const
{
    if (m_extensionFilter.empty()) {
        return true;
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto &filter : m_extensionFilter) {
        if (ext == filter) {
            return true;
        }
    }
    return false;
}

const char *FileExplorer::getFileIcon(const std::filesystem::path &path, bool isDirectory) const
{
    if (isDirectory) {
        return ICON_MD_FOLDER;
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") {
        return ICON_MD_VIEW_IN_AR;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
        return ICON_MD_IMAGE;
    }
    if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp") {
        return ICON_MD_CODE;
    }

    return ICON_MD_INSERT_DRIVE_FILE;
}
