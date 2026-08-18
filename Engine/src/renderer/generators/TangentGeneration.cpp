#include "TangentGeneration.h"

#include "core/utils/Log.h"

#include <cmath>

namespace Rapture {

namespace generator {

static constexpr float UV_AREA_EPSILON = 1e-12f;
static constexpr float LENGTH_EPSILON = 1e-8f;

/**
 * @brief A unit vector perpendicular to a normal
 * @param normal The unit normal to build against
 * @return A unit vector orthogonal to the normal
 */
static glm::vec3 s_perpendicularTo(const glm::vec3 &normal)
{
    glm::vec3 axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::cross(axis, normal));
}

/**
 * @brief Turn the summed frame of every triangle sharing a vertex into one orthonormal tangent
 * @param normal The vertex normal, not required to be unit length
 * @param tangentSum The accumulated tangents
 * @param bitangentSum The accumulated bitangents, only its sign against the tangent frame is kept
 * @return The unit tangent in xyz and the bitangent handedness in w
 */
static glm::vec4 s_resolveTangent(const glm::vec3 &normal, const glm::vec3 &tangentSum, const glm::vec3 &bitangentSum)
{
    glm::vec3 unitNormal = glm::dot(normal, normal) < LENGTH_EPSILON ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::normalize(normal);

    glm::vec3 tangent = tangentSum - unitNormal * glm::dot(unitNormal, tangentSum);
    if (glm::dot(tangent, tangent) < LENGTH_EPSILON) {
        tangent = s_perpendicularTo(unitNormal);
    } else {
        tangent = glm::normalize(tangent);
    }

    float handedness = glm::dot(glm::cross(unitNormal, tangent), bitangentSum) < 0.0f ? -1.0f : 1.0f;
    return glm::vec4(tangent, handedness);
}

template <typename IndexType>
std::vector<glm::vec4> generateTangents(std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
                                        std::span<const glm::vec2> texCoords, std::span<const IndexType> indices)
{
    const size_t vertexCount = positions.size();
    if (vertexCount == 0 || normals.size() != vertexCount || texCoords.size() != vertexCount) {
        RP_CORE_ERROR("Cannot generate tangents for {} positions against {} normals and {} texture coordinates", positions.size(),
                      normals.size(), texCoords.size());
        return {};
    }

    if (indices.size() % 3 != 0) {
        RP_CORE_ERROR("Cannot generate tangents, {} indices do not form whole triangles", indices.size());
        return {};
    }

    std::vector<glm::vec3> tangentSums(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> bitangentSums(vertexCount, glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            RP_CORE_ERROR("Cannot generate tangents, triangle {} addresses a vertex outside the {} available", i / 3, vertexCount);
            return {};
        }

        const glm::vec3 edge1 = positions[i1] - positions[i0];
        const glm::vec3 edge2 = positions[i2] - positions[i0];
        const glm::vec2 deltaUV1 = texCoords[i1] - texCoords[i0];
        const glm::vec2 deltaUV2 = texCoords[i2] - texCoords[i0];

        // Zero signed uv area means the triangle collapses in texture space and pins no direction
        const float determinant = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(determinant) < UV_AREA_EPSILON) {
            continue;
        }

        const float scale = 1.0f / determinant;
        const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * scale;
        const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * scale;

        for (uint32_t index : {i0, i1, i2}) {
            tangentSums[index] += tangent;
            bitangentSums[index] += bitangent;
        }
    }

    std::vector<glm::vec4> tangents(vertexCount);
    for (size_t v = 0; v < vertexCount; v++) {
        tangents[v] = s_resolveTangent(normals[v], tangentSums[v], bitangentSums[v]);
    }

    return tangents;
}

template std::vector<glm::vec4> generateTangents<uint8_t>(std::span<const glm::vec3>, std::span<const glm::vec3>,
                                                          std::span<const glm::vec2>, std::span<const uint8_t>);
template std::vector<glm::vec4> generateTangents<uint16_t>(std::span<const glm::vec3>, std::span<const glm::vec3>,
                                                           std::span<const glm::vec2>, std::span<const uint16_t>);
template std::vector<glm::vec4> generateTangents<uint32_t>(std::span<const glm::vec3>, std::span<const glm::vec3>,
                                                           std::span<const glm::vec2>, std::span<const uint32_t>);

} // namespace generator

} // namespace Rapture
