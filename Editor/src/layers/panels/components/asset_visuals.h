#ifndef RAPTURE__ASSET_VISUALS_H
#define RAPTURE__ASSET_VISUALS_H

#include "asset_manager/AssetCommon.h"

#include <modules/color.h>

namespace Rapture {
struct TypeInfo;
} // namespace Rapture

/**
 * @brief Accent color identifying an asset type
 * @param type The asset type
 * @return The color for that type
 */
Amethyst::Color3 Asset_colorForType(Rapture::AssetType type);

/**
 * @brief Icon standing in for an asset of the given type when it has no thumbnail
 * @param type The asset type
 * @param authoredClass The class a module holds, which picks the icon ahead of the type
 * @return SVG source for that type's icon
 */
const char *Asset_iconForType(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass = nullptr);

/**
 * @brief A scene object class's icon and the theme class colouring it
 */
struct SceneObjectIcon {
    const char *svg;
    const char *styleClass;
};

/**
 * @brief Icon standing in for a scene object class, taken from the nearest base that has one
 * @param authoredClass The class to find an icon for
 * @return The icon and the theme class it takes its colour from
 */
SceneObjectIcon SceneObject_iconForClass(const Rapture::TypeInfo *authoredClass);

#endif // RAPTURE__ASSET_VISUALS_H
