#include "asset_visuals.h"

#include "Icons.h"

#include "modules/controllers/CameraController.h"

struct ModuleIcon {
    const Rapture::TypeInfo *moduleClass;
    const char *svg;
};

static const ModuleIcon MODULE_ICONS[] = {
    {&Rapture::CameraController::staticType(), Icons::SVG_CAMERA},
    {&Rapture::Controller::staticType(), Icons::SVG_CONTROLLER},
};

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
    case Rapture::ASSET_MESH:
        return Amethyst::Color3(0.95f, 0.60f, 0.25f); // orange
    case Rapture::ASSET_PREFAB:
        return Amethyst::Color3(0.50f, 0.50f, 0.95f); // periwinkle
    case Rapture::ASSET_ANIMATION:
        return Amethyst::Color3(0.95f, 0.82f, 0.30f); // yellow
    case Rapture::ASSET_AUDIO:
        return Amethyst::Color3(0.25f, 0.82f, 0.72f); // teal
    case Rapture::ASSET_VIDEO:
        return Amethyst::Color3(0.95f, 0.35f, 0.35f); // red
    case Rapture::ASSET_SCENE:
        return Amethyst::Color3(0.72f, 0.85f, 0.30f); // lime
    case Rapture::ASSET_MODULE:
        return Amethyst::Color3(0.93f, 0.52f, 0.42f); // coral
    default:
        return Amethyst::Color3(0.55f, 0.55f, 0.55f); // gray
    }
}

static const char *s_moduleIcon(const Rapture::TypeInfo *moduleClass)
{
    for (const Rapture::TypeInfo *cls = moduleClass; cls != nullptr; cls = cls->base) {
        for (const ModuleIcon &icon : MODULE_ICONS) {
            if (icon.moduleClass == cls) {
                return icon.svg;
            }
        }
    }

    return Icons::SVG_MODULE;
}

const char *Asset_iconForType(Rapture::AssetType type, const Rapture::TypeInfo *moduleClass)
{
    if (type == Rapture::ASSET_MODULE) {
        return s_moduleIcon(moduleClass);
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
    case Rapture::ASSET_MESH:
        return Icons::SVG_MESH;
    case Rapture::ASSET_PREFAB:
        return Icons::SVG_CUBE;
    case Rapture::ASSET_ANIMATION:
        return Icons::SVG_PLAY;
    case Rapture::ASSET_AUDIO:
        return Icons::SVG_AUDIO;
    case Rapture::ASSET_VIDEO:
        return Icons::SVG_CAMERA;
    case Rapture::ASSET_SCENE:
        return Icons::SVG_SCENE;
    default:
        return Icons::SVG_COPY;
    }
}
