#include "AssetVisuals.h"

#include "Icons.h"

#include <rapture/assets/asset_manager/AssetRegistry.h>
#include <rapture/scene/instances/Bone3D.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/Environment.h>
#include <rapture/scene/instances/Folder.h>
#include <rapture/scene/instances/Light3D.h>
#include <rapture/scene/instances/Mesh3D.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/PrimitiveMesh3D.h>
#include <rapture/scene/instances/SpringArm3D.h>
#include <rapture/scene/instances/Terrain3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

static constexpr const char *ICON_CLASS_DEFAULT = "treeview-icon";
static constexpr const char *ICON_CLASS_FOLDER = "treeview-icon-folder";

struct AuthoredIcon {
    const Rapture::TypeInfo *authoredClass;
    const char *svg;
    const char *styleClass = ICON_CLASS_DEFAULT;
};

// most derived first, the lookup walks a class up its bases and takes the first match at each level
static const AuthoredIcon AUTHORED_ICONS[] = {
    {&Rapture::CameraController::staticType(), Icons::SVG_CAMERA},
    {&Rapture::Controller::staticType(), Icons::SVG_CONTROLLER},
    {&Rapture::Folder::staticType(), Icons::SVG_FOLDER, ICON_CLASS_FOLDER},
    {&Rapture::Environment::staticType(), Icons::SVG_WORLD},
    {&Rapture::Camera3D::staticType(), Icons::SVG_CAMERA},
    {&Rapture::SpringArm3D::staticType(), Icons::SVG_LINK},
    {&Rapture::Terrain3D::staticType(), Icons::SVG_GRID},
    {&Rapture::PrimitiveMesh3D::staticType(), Icons::SVG_CUBE},
    {&Rapture::Mesh3D::staticType(), Icons::SVG_MESH},
    {&Rapture::Light3D::staticType(), Icons::SVG_LIGHT},
    {&Rapture::Bone3D::staticType(), Icons::SVG_BONE},
    {&Rapture::Node3D::staticType(), Icons::SVG_TRANSFORM},
};

static constexpr SceneComponentDescription SCENE_COMPONENT_DESCRIPTIONS[] = {
    {"RigidBody3D", "Rigid Body", Icons::SVG_CUBE, "Moved by physics, pushed by forces and collisions",
     "The object is simulated: gravity, forces and collisions move it, and the simulation writes its transform back."},
    {"CharacterBody3D", "Character Body", Icons::SVG_CONTROLLER, "Walks, jumps and collides, moved by a controller",
     "The object walks, jumps and collides under a controller instead of being pushed around by the simulation."},
    {"ScriptComponent", "Script", Icons::SVG_SCRIPT, "Runs a Lua script with this object as its owner",
     "Runs a Lua script with this object as its owner, connected to its events while the world plays."},
};

const SceneComponentDescription *SceneComponent_describeClass(std::string_view className)
{
    for (const SceneComponentDescription &description : SCENE_COMPONENT_DESCRIPTIONS) {
        if (description.className == className) {
            return &description;
        }
    }
    return nullptr;
}

Amethyst::Color3 Asset_colorForType(Rapture::AssetType type)
{
    switch (type) {
    case Rapture::ASSET_TEXTURE:
        return Amethyst::Color3(0.92f, 0.40f, 0.78f); // magenta
    case Rapture::ASSET_CUBEMAP:
        return Amethyst::Color3(0.30f, 0.68f, 0.98f); // azure
    case Rapture::ASSET_SHADER:
        return Amethyst::Color3(0.45f, 0.85f, 0.45f); // green
    case Rapture::ASSET_MATERIAL:
    case Rapture::ASSET_MATERIAL_INSTANCE:
        return Amethyst::Color3(0.68f, 0.45f, 0.95f); // violet
    case Rapture::ASSET_STATIC_MESH:
    case Rapture::ASSET_SKELETAL_MESH:
        return Amethyst::Color3(0.95f, 0.60f, 0.25f); // orange
    case Rapture::ASSET_MODULE:
        return Amethyst::Color3(0.50f, 0.50f, 0.95f); // periwinkle
    case Rapture::ASSET_ANIMATION:
        return Amethyst::Color3(0.95f, 0.82f, 0.30f); // yellow
    case Rapture::ASSET_AUDIO:
        return Amethyst::Color3(0.25f, 0.82f, 0.72f); // teal
    case Rapture::ASSET_VIDEO:
        return Amethyst::Color3(0.95f, 0.35f, 0.35f); // red
    case Rapture::ASSET_WORLD:
        return Amethyst::Color3(0.72f, 0.85f, 0.30f); // lime
    case Rapture::ASSET_SCRIPT:
        return Amethyst::Color3(0.35f, 0.75f, 0.85f); // cyan
    default:
        return Amethyst::Color3(0.55f, 0.55f, 0.55f); // gray
    }
}

SceneObjectIcon SceneObject_iconForClass(const Rapture::TypeInfo *authoredClass)
{
    for (const Rapture::TypeInfo *cls = authoredClass; cls != nullptr; cls = cls->base) {
        for (const AuthoredIcon &icon : AUTHORED_ICONS) {
            if (icon.authoredClass == cls) {
                return {icon.svg, icon.styleClass};
            }
        }
    }

    return {Icons::SVG_CUBE, ICON_CLASS_DEFAULT};
}

std::string Asset_labelForType(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass)
{
    if (authoredClass != nullptr && type == Rapture::ASSET_MODULE) {
        return std::string(authoredClass->name);
    }
    return std::string(Rapture::AssetRegistry::displayName(type));
}

const char *Asset_iconForType(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass)
{
    if (type == Rapture::ASSET_MODULE) {
        return SceneObject_iconForClass(authoredClass).svg;
    }

    switch (type) {
    case Rapture::ASSET_TEXTURE:
        return Icons::SVG_LAYERS;
    case Rapture::ASSET_CUBEMAP:
        return Icons::SVG_CUBE;
    case Rapture::ASSET_SHADER:
        return Icons::SVG_SCRIPT;
    case Rapture::ASSET_MATERIAL:
    case Rapture::ASSET_MATERIAL_INSTANCE:
        return Icons::SVG_MATERIAL;
    case Rapture::ASSET_STATIC_MESH:
    case Rapture::ASSET_SKELETAL_MESH:
        return Icons::SVG_MESH;
    case Rapture::ASSET_SKELETON:
        return Icons::SVG_SKELETON;
    case Rapture::ASSET_MODULE:
        return Icons::SVG_CUBE;
    case Rapture::ASSET_ANIMATION:
        return Icons::SVG_PLAY;
    case Rapture::ASSET_AUDIO:
        return Icons::SVG_AUDIO;
    case Rapture::ASSET_VIDEO:
        return Icons::SVG_CAMERA;
    case Rapture::ASSET_WORLD:
        return Icons::SVG_SCENE;
    case Rapture::ASSET_SCRIPT:
        return Icons::SVG_SCRIPT;
    default:
        return Icons::SVG_COPY;
    }
}
