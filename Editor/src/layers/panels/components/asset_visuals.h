#ifndef RAPTURE__ASSET_VISUALS_H
#define RAPTURE__ASSET_VISUALS_H

#include "asset_manager/AssetCommon.h"

#include <modules/color.h>

/**
 * @brief Accent color identifying an asset type
 * @param type The asset type
 * @return The color for that type
 */
Amethyst::Color3 Asset_colorForType(Rapture::AssetType type);

/**
 * @brief Icon standing in for an asset of the given type when it has no thumbnail
 * @param type The asset type
 * @return SVG source for that type's icon
 */
const char *Asset_iconForType(Rapture::AssetType type);

#endif // RAPTURE__ASSET_VISUALS_H
