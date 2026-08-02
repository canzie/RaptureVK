#include "asset_visuals.h"

#include "Icons.h"

Amethyst::Color3 Asset_colorForType(Rapture::AssetType type)
{
    switch (type) {
    case Rapture::AssetType::TEXTURE:
        return Amethyst::Color3(0.92f, 0.40f, 0.78f); // magenta
    case Rapture::AssetType::CUBEMAP:
        return Amethyst::Color3(0.30f, 0.68f, 0.98f); // azure
    case Rapture::AssetType::SHADER:
        return Amethyst::Color3(0.45f, 0.85f, 0.45f); // green
    case Rapture::AssetType::MATERIAL:
    case Rapture::AssetType::MATERIAL_INSTANCE:
        return Amethyst::Color3(0.68f, 0.45f, 0.95f); // violet
    case Rapture::AssetType::MESH:
        return Amethyst::Color3(0.95f, 0.60f, 0.25f); // orange
    case Rapture::AssetType::PREFAB:
        return Amethyst::Color3(0.50f, 0.50f, 0.95f); // periwinkle
    case Rapture::AssetType::ANIMATION:
        return Amethyst::Color3(0.95f, 0.82f, 0.30f); // yellow
    case Rapture::AssetType::AUDIO:
        return Amethyst::Color3(0.25f, 0.82f, 0.72f); // teal
    case Rapture::AssetType::VIDEO:
        return Amethyst::Color3(0.95f, 0.35f, 0.35f); // red
    case Rapture::AssetType::SCENE:
        return Amethyst::Color3(0.72f, 0.85f, 0.30f); // lime
    default:
        return Amethyst::Color3(0.55f, 0.55f, 0.55f); // gray
    }
}

const char *Asset_iconForType(Rapture::AssetType type)
{
    switch (type) {
    case Rapture::AssetType::TEXTURE:
        return Icons::SVG_LAYERS;
    case Rapture::AssetType::CUBEMAP:
        return Icons::SVG_CUBE;
    case Rapture::AssetType::SHADER:
        return Icons::SVG_SCRIPT;
    case Rapture::AssetType::MATERIAL:
    case Rapture::AssetType::MATERIAL_INSTANCE:
        return Icons::SVG_MATERIAL;
    case Rapture::AssetType::MESH:
        return Icons::SVG_MESH;
    case Rapture::AssetType::PREFAB:
        return Icons::SVG_CUBE;
    case Rapture::AssetType::ANIMATION:
        return Icons::SVG_PLAY;
    case Rapture::AssetType::AUDIO:
        return Icons::SVG_AUDIO;
    case Rapture::AssetType::VIDEO:
        return Icons::SVG_CAMERA;
    case Rapture::AssetType::SCENE:
        return Icons::SVG_SCENE;
    default:
        return Icons::SVG_COPY;
    }
}
