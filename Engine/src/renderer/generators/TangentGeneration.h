#ifndef RAPTURE__TANGENT_GENERATION_H
#define RAPTURE__TANGENT_GENERATION_H

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace Rapture {

namespace generator {

/**
 * @brief Build a per-vertex tangent frame for an indexed triangle list
 * @param positions Vertex positions
 * @param normals Vertex normals, one per position
 * @param texCoords Vertex texture coordinates, one per position
 * @param indices Triangle list indices addressing the vertex arrays
 * @return One vec4 per vertex, xyz the unit tangent and w the bitangent handedness, empty when the inputs disagree
 *
 * Instantiated for uint8_t, uint16_t and uint32_t indices.
 */
template <typename IndexType>
std::vector<glm::vec4> generateTangents(std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
                                        std::span<const glm::vec2> texCoords, std::span<const IndexType> indices);

} // namespace generator

} // namespace Rapture

#endif // RAPTURE__TANGENT_GENERATION_H
