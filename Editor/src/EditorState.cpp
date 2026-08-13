#include "EditorState.h"

#include "logging/Log.h"
#include "scenes/Project.h"

#include <utility>

static constexpr std::string_view KEY_WORKSPACES = "openWorkspaces";
static constexpr std::string_view KEY_KIND = "kind";
static constexpr std::string_view KEY_ACTIVE = "active";
static constexpr std::string_view KEY_HANDLE = "asset";

EditorState EditorState::load(const Rapture::Project &project)
{
    EditorState state;

    Rapture::ReadNode root = project.getEditorSection();
    if (!root.valid()) {
        return state;
    }

    Rapture::ReadNode workspaces = root.child(KEY_WORKSPACES);
    state.workspaces.reserve(workspaces.size());

    for (size_t i = 0; i < workspaces.size(); i++) {
        Rapture::ReadNode entry = workspaces.at(i);
        std::string_view kind = entry.child(KEY_KIND).asString();
        if (kind.empty()) {
            continue;
        }
        state.workspaces.push_back({std::string(kind), entry.child(KEY_ACTIVE).asBool(false),
                                    entry.child(KEY_HANDLE).asU64(Rapture::INVALID_ASSET_HANDLE)});
    }

    return state;
}

void EditorState::store(Rapture::Project &project) const
{
    Rapture::SerialDocument doc;
    Rapture::WriteNode root = doc.root();
    Rapture::WriteNode workspaceList = root.addArray(KEY_WORKSPACES);

    for (const EditorWorkspaceState &workspace : workspaces) {
        Rapture::WriteNode entry = workspaceList.appendObject();
        entry.set(KEY_KIND, std::string_view(workspace.kind));
        entry.set(KEY_ACTIVE, workspace.active);
        if (workspace.handle != Rapture::INVALID_ASSET_HANDLE) {
            entry.set(KEY_HANDLE, static_cast<uint64_t>(workspace.handle));
        }
    }

    if (!doc.freeze()) {
        RP_ERROR("failed to build the editor section, the layout will not be restored");
        return;
    }

    project.setEditorSection(std::move(doc));
}
