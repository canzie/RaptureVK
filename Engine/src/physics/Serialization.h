#ifndef RAPTURE__PHYSICS_SERIALIZATION_H
#define RAPTURE__PHYSICS_SERIALIZATION_H

#include "physics/Common.h"
#include "serialization/SerialDocument.h"

namespace Rapture {
namespace physics {

/**
 * @brief Writes a collision shape's type and its measurements
 * @param node Cursor to the object the shape is written into
 * @param shape The shape to write
 */
void CollisionShape_serialize(WriteNode node, const CollisionShape &shape);

/**
 * @brief Reads a collision shape back
 * @param node Cursor to the object the shape was written into
 * @param fallback Supplies the type when the document names none
 * @return The shape
 */
CollisionShape CollisionShape_deserialize(ReadNode node, const CollisionShape &fallback);

} // namespace physics
} // namespace Rapture

#endif // RAPTURE__PHYSICS_SERIALIZATION_H
