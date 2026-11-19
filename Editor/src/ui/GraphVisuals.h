#ifndef RAPTURE__GRAPH_VISUALS_H
#define RAPTURE__GRAPH_VISUALS_H

#include <rapture/assets/materials/graph/MaterialGraphTypes.h>

#include <modules/color.h>

#include <string_view>

/**
 * @brief The family a material graph node belongs to, grouping it in the add node menu and tinting its header
 */
enum GraphCategory {
    GRAPH_CATEGORY_INPUT,
    GRAPH_CATEGORY_TEXTURE,
    GRAPH_CATEGORY_MATH,
    GRAPH_CATEGORY_VECTOR,
    GRAPH_CATEGORY_COLOR,
    GRAPH_CATEGORY_OUTPUT,
    GRAPH_CATEGORY_COUNT
};

/**
 * @brief Name of a node category as the add node menu lists it
 * @param category The category
 * @return The category's name
 */
std::string_view GraphCategory_name(GraphCategory category);

/**
 * @brief Icon shown in the header of a node of the given category
 * @param category The category
 * @return SVG source of the icon
 */
const char *GraphCategory_icon(GraphCategory category);

/**
 * @brief Accent color identifying a node category
 * @param category The category
 * @return The category's color
 */
Amethyst::Color3 GraphCategory_color(GraphCategory category);

/**
 * @brief Fill of the header of a node of the given category, the category's color fading out to the right
 * @param category The category
 * @return The header fill
 */
Amethyst::Color3 GraphCategory_headerFill(GraphCategory category);

/**
 * @brief Color of a pin and the wires leaving it, by the type of value it carries
 * @param type The pin type
 * @return The pin's color
 */
Amethyst::Color3 PinType_color(Rapture::PinType type);

#endif // RAPTURE__GRAPH_VISUALS_H
