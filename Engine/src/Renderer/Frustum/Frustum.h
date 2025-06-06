#pragma once

#include <glm/glm.hpp>
#include <array>

namespace Rapture
{
    // Forward declaration
    class BoundingBox;

    enum class FrustumResult
    {
        Inside,
        Intersect,
        Outside
    };

    class Frustum
    {
    public:
        Frustum() = default;
        ~Frustum() = default;

        // Update the frustum planes using view and projection matrices
        void update(const glm::mat4& projection, const glm::mat4& view);

        // Test if a bounding box is inside, outside, or intersecting the frustum
        FrustumResult testBoundingBox(const BoundingBox& boundingBox) const;


        

    private:
        // Frustum planes in this order: Left, Right, Bottom, Top, Near, Far
        std::array<glm::vec4, 6> _planes;
    };
} 