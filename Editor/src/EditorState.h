#ifndef RAPTURE__EDITOR_STATE_H
#define RAPTURE__EDITOR_STATE_H

#include "asset_manager/AssetCommon.h"

#include <string>
#include <vector>

namespace Rapture {
class Project;
}

/**
 * @brief A workspace that was open, and whether it was the one in front
 */
struct EditorWorkspaceState {
    std::string kind;
    bool active = false;
    // the asset the workspace was editing, invalid for the workspaces that edit no single asset
    Rapture::AssetHandle handle = Rapture::INVALID_ASSET_HANDLE;
};

/**
 * @brief What the editor puts back on the next run, held in the project file under the editor's own key
 */
class EditorState {
  public:
    /**
     * @brief Reads the editor's section of a project
     * @param project The project to read from
     * @return The stored state, empty when the project holds none
     */
    static EditorState load(const Rapture::Project &project);

    /**
     * @brief Writes this state into the project's editor section, which the caller then has to save
     * @param project The project to write into
     */
    void store(Rapture::Project &project) const;

  public:
    std::vector<EditorWorkspaceState> workspaces;
};

#endif // RAPTURE__EDITOR_STATE_H
