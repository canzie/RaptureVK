#include "physics/Serialization.h"

namespace Rapture {
namespace physics {

static constexpr std::string_view KEY_SHAPE = "shape";
static constexpr std::string_view KEY_HALF_EXTENTS = "halfExtents";
static constexpr std::string_view KEY_RADIUS = "radius";
static constexpr std::string_view KEY_HALF_HEIGHT = "halfHeight";

void CollisionShape_serialize(WriteNode node, const CollisionShape &shape)
{
    node.set(KEY_SHAPE, CollisionShape_toString(CollisionShape_typeOf(shape)));

    if (const auto *box = std::get_if<BoxShape>(&shape)) {
        WriteNode extents = node.addArray(KEY_HALF_EXTENTS);
        extents.append(box->halfExtents.x);
        extents.append(box->halfExtents.y);
        extents.append(box->halfExtents.z);
        return;
    }

    if (const auto *sphere = std::get_if<SphereShape>(&shape)) {
        node.set(KEY_RADIUS, sphere->radius);
        return;
    }

    if (const auto *capsule = std::get_if<CapsuleShape>(&shape)) {
        node.set(KEY_RADIUS, capsule->radius);
        node.set(KEY_HALF_HEIGHT, capsule->halfHeight);
    }
}

CollisionShape CollisionShape_deserialize(ReadNode node, const CollisionShape &fallback)
{
    const CollisionShapeType type =
        CollisionShape_fromString(node.child(KEY_SHAPE).asString(), CollisionShape_typeOf(fallback));

    if (type == COLLISION_SHAPE_SPHERE) {
        return SphereShape{static_cast<float>(node.child(KEY_RADIUS).asF64(0.5))};
    }

    if (type == COLLISION_SHAPE_CAPSULE) {
        return CapsuleShape{static_cast<float>(node.child(KEY_HALF_HEIGHT).asF64(0.5)),
                            static_cast<float>(node.child(KEY_RADIUS).asF64(0.5))};
    }

    ReadNode extents = node.child(KEY_HALF_EXTENTS);
    if (extents.size() != 3) {
        return BoxShape{};
    }

    return BoxShape{glm::vec3(static_cast<float>(extents.at(0).asF64(0.5)), static_cast<float>(extents.at(1).asF64(0.5)),
                              static_cast<float>(extents.at(2).asF64(0.5)))};
}

} // namespace physics
} // namespace Rapture
