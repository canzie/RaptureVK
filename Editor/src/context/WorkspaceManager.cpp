#include "WorkspaceManager.h"

#include "workspaces/AnimationsWorkspace.h"
#include "workspaces/MaterialEditorWorkspace.h"
#include "workspaces/ModuleEditorWorkspace.h"
#include "workspaces/ScriptingWorkspace.h"
#include "workspaces/SkeletalMeshWorkspace.h"
#include "workspaces/SkeletonWorkspace.h"
#include "workspaces/StaticMeshWorkspace.h"
#include "workspaces/TextureWorkspace.h"
#include "workspaces/WorldEditorWorkspace.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/AssetRegistry.h>
#include <rapture/assets/worlds/AWorld.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/scene/Project.h>

#include <algorithm>

WorkspaceManager::WorkspaceManager(const EditorContext &context) : m_editor(&context) {}

WorkspaceManager::~WorkspaceManager()
{
    if (m_tabBar != nullptr) {
        m_tabBar->onSelectionChanged = nullptr;
        m_tabBar->onTabClosed = nullptr;
    }
    m_active = nullptr;
    m_workspaces.clear();
}

void WorkspaceManager::attach(Amethyst::TabBar &tabBar)
{
    m_tabBar = &tabBar;
    m_tabBar->onSelectionChanged = [this](int32_t index) { onTabSelected(m_tabBar->getTabContent(index)); };
    m_tabBar->onTabClosed = [this](Amethyst::Instance *content) { closeWorkspace(content); };
}

void WorkspaceManager::setContentArea(Amethyst::vec2 position, Amethyst::vec2 size)
{
    m_contentAreaPosition = position;
    m_contentAreaSize = size;

    for (auto &workspace : m_workspaces) {
        workspace->setContentArea(position, size);
    }
}

void WorkspaceManager::restore(const EditorState &state)
{
    RP_ASSERT(m_tabBar != nullptr, "workspaces cannot be restored before the workspace bar exists");

    for (const EditorWorkspaceState &entry : state.workspaces) {
        openStoredWorkspace(entry);
    }

    const bool hasWorldEditor = std::any_of(m_workspaces.begin(), m_workspaces.end(), [](const auto &workspace) {
        return dynamic_cast<WorldEditorWorkspace *>(workspace.get()) != nullptr;
    });
    if (!hasWorldEditor) {
        openAsset(m_editor->project->getStartupWorld());
    }

    if (m_workspaces.empty()) {
        return;
    }

    Workspace *front = m_workspaces.front().get();
    auto wanted = std::find_if(state.workspaces.begin(), state.workspaces.end(), [](const auto &entry) { return entry.active; });
    if (wanted != state.workspaces.end()) {
        for (auto &workspace : m_workspaces) {
            auto *assetWorkspace = dynamic_cast<AssetWorkspace *>(workspace.get());
            const bool matches =
                assetWorkspace != nullptr ? assetWorkspace->handle() == wanted->handle : workspace->tabLabel() == wanted->kind;
            if (matches) {
                front = workspace.get();
                break;
            }
        }
    }

    select(*front);
}

void WorkspaceManager::openStoredWorkspace(const EditorWorkspaceState &entry)
{
    if (entry.handle != Rapture::INVALID_ASSET_HANDLE) {
        openAsset(entry.handle);
        return;
    }

    if (entry.kind == ScriptingWorkspace::LABEL) {
        adopt(std::make_unique<ScriptingWorkspace>(*m_editor));
    } else if (entry.kind == AnimationsWorkspace::LABEL) {
        adopt(std::make_unique<AnimationsWorkspace>(*m_editor));
    } else {
        RP_WARN("dropping stored workspace '{}', this build has no such workspace", entry.kind);
    }
}

EditorState WorkspaceManager::captureState() const
{
    EditorState state;
    state.workspaces.reserve(m_workspaces.size());

    for (const auto &workspace : m_workspaces) {
        EditorWorkspaceState entry;
        entry.active = workspace.get() == m_active;
        if (auto *assetWorkspace = dynamic_cast<AssetWorkspace *>(workspace.get())) {
            entry.handle = assetWorkspace->handle();
        } else {
            entry.kind = std::string(workspace->tabLabel());
        }
        state.workspaces.push_back(std::move(entry));
    }

    return state;
}

void WorkspaceManager::saveDockLayouts() const
{
    for (const auto &workspace : m_workspaces) {
        workspace->saveDockLayout();
    }
}

void WorkspaceManager::onUpdate(float dt)
{
    for (auto &workspace : m_workspaces) {
        workspace->onUpdate(dt);
    }
}

void WorkspaceManager::openAsset(Rapture::AssetHandle handle)
{
    if (handle == Rapture::INVALID_ASSET_HANDLE || m_tabBar == nullptr) {
        return;
    }

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle);

    if (metadata.assetType == Rapture::ASSET_SCRIPT) {
        ScriptingWorkspace *scripting = findScriptingWorkspace();
        if (scripting == nullptr) {
            scripting = static_cast<ScriptingWorkspace *>(adopt(std::make_unique<ScriptingWorkspace>(*m_editor)));
        }
        select(*scripting);
        scripting->openScript(handle);
        return;
    }

    if (AssetWorkspace *open = findAssetWorkspace(handle)) {
        select(*open);
        return;
    }

    std::unique_ptr<Workspace> workspace = createAssetWorkspace(handle);
    if (workspace != nullptr) {
        adopt(std::move(workspace));
    }
}

std::unique_ptr<Workspace> WorkspaceManager::createAssetWorkspace(Rapture::AssetHandle handle) const
{
    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle);

    switch (metadata.assetType) {
    case Rapture::ASSET_MODULE:
        return std::make_unique<ModuleEditorWorkspace>(*m_editor, handle);
    case Rapture::ASSET_MATERIAL_INSTANCE:
        return std::make_unique<MaterialEditorWorkspace>(*m_editor, handle);
    case Rapture::ASSET_STATIC_MESH:
        return std::make_unique<StaticMeshWorkspace>(*m_editor, handle);
    case Rapture::ASSET_SKELETAL_MESH:
        return std::make_unique<SkeletalMeshWorkspace>(*m_editor, handle);
    case Rapture::ASSET_SKELETON:
        return std::make_unique<SkeletonWorkspace>(*m_editor, handle);
    case Rapture::ASSET_TEXTURE:
        return std::make_unique<TextureWorkspace>(*m_editor, handle);
    case Rapture::ASSET_WORLD: {
        Rapture::Ref<Rapture::AWorld> world = Rapture::AssetManager::getAsset<Rapture::AWorld>(handle);
        if (!world) {
            RP_WARN("world '{}' could not be loaded, not opening a world editor for it", metadata.getName());
            return nullptr;
        }
        return std::make_unique<WorldEditorWorkspace>(*m_editor, std::move(world));
    }
    default:
        RP_WARN("'{}' assets have no workspace to open in", Rapture::AssetRegistry::displayName(metadata.assetType));
        return nullptr;
    }
}

Workspace *WorkspaceManager::adopt(std::unique_ptr<Workspace> workspace)
{
    Workspace *adopted = workspace.get();
    adopted->assignId(m_nextWorkspaceId++);
    adopted->setContentArea(m_contentAreaPosition, m_contentAreaSize);

    // held before it is attached, so the selection handler can find the workspace the new tab belongs to
    m_workspaces.push_back(std::move(workspace));
    adopted->attach(*m_tabBar);
    select(*adopted);

    return adopted;
}

AssetWorkspace *WorkspaceManager::findAssetWorkspace(Rapture::AssetHandle handle) const
{
    for (const auto &workspace : m_workspaces) {
        auto *assetWorkspace = dynamic_cast<AssetWorkspace *>(workspace.get());
        if (assetWorkspace != nullptr && assetWorkspace->handle() == handle) {
            return assetWorkspace;
        }
    }
    return nullptr;
}

ScriptingWorkspace *WorkspaceManager::findScriptingWorkspace() const
{
    for (const auto &workspace : m_workspaces) {
        if (auto *scripting = dynamic_cast<ScriptingWorkspace *>(workspace.get())) {
            return scripting;
        }
    }
    return nullptr;
}

void WorkspaceManager::select(Workspace &workspace)
{
    m_tabBar->select(workspace.getContainer());
    onTabSelected(workspace.getContainer());
}

void WorkspaceManager::onTabSelected(Amethyst::Instance *content)
{
    Workspace *selected = nullptr;
    for (auto &workspace : m_workspaces) {
        const bool front = workspace->getContainer() == content;
        workspace->setActive(front);
        if (front) {
            selected = workspace.get();
        }
    }

    if (selected == m_active) {
        return;
    }

    m_active = selected;
    onActiveWorkspaceChanged.fire(m_active);
}

void WorkspaceManager::closeWorkspace(Amethyst::Instance *content)
{
    auto it = std::find_if(m_workspaces.begin(), m_workspaces.end(),
                           [content](const std::unique_ptr<Workspace> &workspace) { return workspace->getContainer() == content; });
    if (it == m_workspaces.end()) {
        return;
    }

    if (it->get() == m_active) {
        m_active = nullptr;
        onActiveWorkspaceChanged.fire(nullptr);
    }
    m_workspaces.erase(it);
}
