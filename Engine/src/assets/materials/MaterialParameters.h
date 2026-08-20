#ifndef RAPTURE__MATERIAL_PARAMETERS_H
#define RAPTURE__MATERIAL_PARAMETERS_H

#include <string>

namespace Rapture {

/**
 * @brief An author-named material parameter, keyed by string
 */
using ParameterId = std::string;

/**
 * @brief Standard parameter names the glTF base material exposes
 *
 * Material parameters are author-named strings. These constants keep the glTF importer and the
 * glTF base builder in agreement on the well-known PBR channel names.
 */
inline const ParameterId MP_ALBEDO = "albedo";
inline const ParameterId MP_ROUGHNESS = "roughness";
inline const ParameterId MP_METALLIC = "metallic";
inline const ParameterId MP_AO = "ao";
inline const ParameterId MP_EMISSIVE = "emissive";
inline const ParameterId MP_EMISSIVE_STRENGTH = "emissive_strength";

inline const ParameterId MP_ALBEDO_MAP = "albedo_map";
inline const ParameterId MP_NORMAL_MAP = "normal_map";
inline const ParameterId MP_METALLIC_ROUGHNESS_MAP = "metallic_roughness_map";
inline const ParameterId MP_AO_MAP = "ao_map";
inline const ParameterId MP_EMISSIVE_MAP = "emissive_map";
inline const ParameterId MP_HEIGHT_MAP = "height_map";
inline const ParameterId MP_SPECULAR_MAP = "specular_map";

/**
 * @brief Parameter names the grid material exposes
 */
inline const ParameterId MP_GRID_SPACING = "grid_spacing";
inline const ParameterId MP_GRID_SUBDIVISIONS = "grid_subdivisions";
inline const ParameterId MP_GRID_LINE_WIDTH = "grid_line_width";
inline const ParameterId MP_GRID_MINOR_LINE_WIDTH = "grid_minor_line_width";
inline const ParameterId MP_GRID_COLOR = "grid_color";
inline const ParameterId MP_GRID_LINE_COLOR = "grid_line_color";
inline const ParameterId MP_GRID_MINOR_LINE_COLOR = "grid_minor_line_color";

} // namespace Rapture

#endif // RAPTURE__MATERIAL_PARAMETERS_H
