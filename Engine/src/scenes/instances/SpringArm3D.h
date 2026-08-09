#ifndef RAPTURE__SPRINGARM3D_H
#define RAPTURE__SPRINGARM3D_H

#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief A pivot holding what hangs from it a set distance behind itself, pulling in to keep it
 * out of terrain and geometry.
 */
class SpringArm3D : public Node3D {
  public:
    SpringArm3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    float length() const { return m_length; }
    void setLength(float length);

    /**
     * @brief Places every child at this arm's length behind the pivot
     */
    // TODO: shorten to the first hit along the arm once Jolt can be cast against
    void applyLength();

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    float m_length = 4.0f;
};

} // namespace Rapture

#endif // RAPTURE__SPRINGARM3D_H
