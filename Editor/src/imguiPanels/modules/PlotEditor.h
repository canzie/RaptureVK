#ifndef RAPTURE__PLOT_EDITOR_H
#define RAPTURE__PLOT_EDITOR_H

#include "Generators/Terrain/TerrainTypes.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <memory>
#include <vector>

namespace Modules {

enum InterpolationType {
    LINEAR,
};

struct SplinePoints {
    std::vector<glm::vec2> *points;
    InterpolationType interpolationType;
};
SplinePoints createSplinePoints(std::vector<glm::vec2> *points, InterpolationType interpolationType);

bool plotEditor(const char *label, const SplinePoints &points, ImVec2 size = ImVec2(0, 200), float minX = -1.0f, float maxX = 1.0f,
                float minY = 0.0f, float maxY = 1.0f);
} // namespace Modules

#endif // RAPTURE__PLOT_EDITOR_H
